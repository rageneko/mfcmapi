#include <StdAfx.h>
#include <MrMapi/MMMapiMime.h>
#include <MrMapi/mmcli.h>
#include <core/mapi/mapiMime.h>
#include <core/utility/import.h>
#include <core/interpret/flags.h>
#include <core/mapi/extraPropTags.h>

void DoMAPIMIME(_In_ cli::MYOPTIONS ProgOpts)
{
	const auto input = cli::switchInput.getArg(0);
	printf("Message File Converter\n");
	printf("Options specified:\n");
	printf("   Input File: %ws\n", input.c_str());
	printf("   Output File: %ws\n", ProgOpts.lpszOutput.c_str());
	printf("   Conversion Type: ");
	if (cli::switchMAPI.isSet())
	{
		printf("MAPI -> MIME\n");

		printf("   Save Format: %s\n", cli::switchRFC822.isSet() ? "RFC822" : "RFC1521");

		if (cli::switchWrap.isSet())
		{
			printf("   Line Wrap: ");
			if (0 == ProgOpts.ulWrapLines)
				printf("OFF\n");
			else
				printf("%lu\n", ProgOpts.ulWrapLines);
		}
	}
	else if (cli::switchMAPI.isSet())
	{
		printf("MIME -> MAPI\n");
		if (cli::switchUnicode.isSet())
		{
			printf("   Building Unicode MSG file\n");
		}
		if (cli::switchCharset.isSet())
		{
			printf("   CodePage: %lu\n", ProgOpts.ulCodePage);
			printf("   CharSetType: ");
			switch (ProgOpts.cSetType)
			{
			case CHARSET_BODY:
				printf("CHARSET_BODY");
				break;
			case CHARSET_HEADER:
				printf("CHARSET_HEADER");
				break;
			case CHARSET_WEB:
				printf("CHARSET_WEB");
				break;
			}
			printf("\n");
			printf("   CharSetApplyType: ");
			switch (ProgOpts.cSetApplyType)
			{
			case CSET_APPLY_UNTAGGED:
				printf("CSET_APPLY_UNTAGGED");
				break;
			case CSET_APPLY_ALL:
				printf("CSET_APPLY_ALL");
				break;
			case CSET_APPLY_TAG_ALL:
				printf("CSET_APPLY_TAG_ALL");
				break;
			}
			printf("\n");
		}
	}

	if (0 != ProgOpts.convertFlags)
	{
		auto szFlags = flags::InterpretFlags(flagCcsf, ProgOpts.convertFlags);
		if (!szFlags.empty())
		{
			printf("   Conversion Flags: %ws\n", szFlags.c_str());
		}
	}

	if (cli::switchEncoding.isSet())
	{
		auto szType = flags::InterpretFlags(flagIet, ProgOpts.ulEncodingType);
		if (!szType.empty())
		{
			printf("   Encoding Type: %ws\n", szType.c_str());
		}
	}

	if (cli::switchAddressBook.isSet())
	{
		printf("   Using Address Book\n");
	}

	auto hRes = S_OK;

	LPADRBOOK lpAdrBook = nullptr;
	if (cli::switchAddressBook.isSet() && ProgOpts.lpMAPISession)
	{
		WC_MAPI(ProgOpts.lpMAPISession->OpenAddressBook(NULL, nullptr, AB_NO_DIALOG, &lpAdrBook));
		if (FAILED(hRes)) printf("OpenAddressBook returned an error: 0x%08lx\n", hRes);
	}

	if (cli::switchMIME.isSet())
	{
		// Source file is MSG, target is EML
		hRes = WC_H(mapi::mapimime::ConvertMSGToEML(
			input.c_str(),
			ProgOpts.lpszOutput.c_str(),
			ProgOpts.convertFlags,
			cli::switchEncoding.isSet() ? static_cast<ENCODINGTYPE>(ProgOpts.ulEncodingType) : IET_UNKNOWN,
			cli::switchRFC822.isSet() ? SAVE_RFC822 : SAVE_RFC1521,
			cli::switchWrap.isSet() ? ProgOpts.ulWrapLines : USE_DEFAULT_WRAPPING,
			lpAdrBook));
	}
	else if (cli::switchMAPI.isSet())
	{
		// Source file is EML, target is MSG
		HCHARSET hCharSet = nullptr;
		if (cli::switchCharset.isSet())
		{
			hRes = WC_H(import::MyMimeOleGetCodePageCharset(ProgOpts.ulCodePage, ProgOpts.cSetType, &hCharSet));
			if (FAILED(hRes))
			{
				printf("MimeOleGetCodePageCharset returned 0x%08lX\n", hRes);
			}
		}

		if (SUCCEEDED(hRes))
		{
			hRes = WC_H(mapi::mapimime::ConvertEMLToMSG(
				input.c_str(),
				ProgOpts.lpszOutput.c_str(),
				ProgOpts.convertFlags,
				cli::switchCharset.isSet(),
				hCharSet,
				ProgOpts.cSetApplyType,
				lpAdrBook,
				cli::switchUnicode.isSet()));
		}
	}

	if (SUCCEEDED(hRes))
	{
		printf("File converted successfully.\n");
	}
	else if (REGDB_E_CLASSNOTREG == hRes)
	{
		printf("MAPI <-> MIME converter not found. Perhaps Outlook is not installed.\n");
	}
	else
	{
		printf("Conversion returned an error: 0x%08lx\n", hRes);
	}

	if (lpAdrBook) lpAdrBook->Release();
}