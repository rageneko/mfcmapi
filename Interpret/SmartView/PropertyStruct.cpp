#include <StdAfx.h>
#include <Interpret/SmartView/PropertyStruct.h>
#include <Interpret/InterpretProp.h>
#include <Interpret/SmartView/SmartView.h>

namespace smartview
{
	PropertyStruct::PropertyStruct() {}

	void PropertyStruct::Parse()
	{
		// For consistancy with previous parsings, we'll refuse to parse if asked to parse more than _MaxEntriesSmall
		// However, we may want to reconsider this choice.
		if (m_MaxEntries > _MaxEntriesSmall) return;

		DWORD dwPropCount = 0;
		for (;;)
		{
			if (dwPropCount >= m_MaxEntries) break;
			m_Props.push_back(BinToSPropValueStruct(false));
			if (!m_Parser.RemainingBytes()) break;
			dwPropCount++;
		}
	}

	void PropertyStruct::ParseBlocks()
	{
		auto i = 0;
		for (auto prop : m_Props)
		{
			if (i != 0)
			{
				addLine();
			}

			addHeader(L"Property[%1!d!]\r\n", i++);
			addBlock(prop.ulPropTag, L"Property = 0x%1!08X!", prop.ulPropTag.getData());

			auto propTagNames = interpretprop::PropTagToPropName(prop.ulPropTag, false);
			if (!propTagNames.bestGuess.empty())
			{
				addLine();
				addBlock(prop.ulPropTag, L"Name: %1!ws!", propTagNames.bestGuess.c_str());
			}

			if (!propTagNames.otherMatches.empty())
			{
				addLine();
				addBlock(prop.ulPropTag, L"Other Matches: %1!ws!", propTagNames.otherMatches.c_str());
			}

			std::wstring PropString;
			std::wstring AltPropString;
			auto sProp = prop.getData();
			interpretprop::InterpretProp(&sProp, &PropString, &AltPropString);

			// TODO: get proper blocks here
			addLine();
			addHeader(L"PropString = %1!ws! ", strings::RemoveInvalidCharactersW(PropString, false).c_str());
			addHeader(L"AltPropString = %1!ws!", strings::RemoveInvalidCharactersW(AltPropString, false).c_str());

			auto szSmartView = InterpretPropSmartView(&sProp, nullptr, nullptr, nullptr, false, false);

			if (!szSmartView.empty())
			{
				// TODO: proper blocks here
				addLine();
				addHeader(L"Smart View: %1!ws!", szSmartView.c_str());
			}
		}
	}

	_Check_return_ SPropValueStruct PropertyStruct::BinToSPropValueStruct(bool bStringPropsExcludeLength)
	{
		const auto ulCurrOffset = m_Parser.GetCurrentOffset();

		auto prop = SPropValueStruct{};
		prop.PropType = m_Parser.GetBlock<WORD>();
		prop.PropID = m_Parser.GetBlock<WORD>();

		prop.ulPropTag = PROP_TAG(prop.PropType, prop.PropID);
		prop.ulPropTag.setSize(prop.PropType.getSize() + prop.PropID.getSize());
		prop.ulPropTag.setOffset(prop.PropType.getOffset());
		prop.dwAlignPad = 0;

		switch (prop.PropType)
		{
		case PT_LONG:
			prop.Value.l = m_Parser.GetBlock<DWORD>();
			break;
		case PT_ERROR:
			prop.Value.err = m_Parser.GetBlock<DWORD>();
			break;
		case PT_BOOLEAN:
			prop.Value.b = m_Parser.GetBlock<WORD>();
			break;
		case PT_UNICODE:
			if (bStringPropsExcludeLength)
			{
				prop.Value.lpszW = m_Parser.GetBlockStringW();
			}
			else
			{
				// This is apparently a cb...
				prop.Value.lpszW = m_Parser.GetBlockStringW(m_Parser.Get<WORD>() / sizeof(WCHAR));
			}
			break;
		case PT_STRING8:
			if (bStringPropsExcludeLength)
			{
				prop.Value.lpszA = m_Parser.GetBlockStringA();
			}
			else
			{
				// This is apparently a cb...
				prop.Value.lpszA = m_Parser.GetBlockStringA(m_Parser.Get<WORD>());
			}
			break;
		case PT_SYSTIME:
			prop.Value.ft.dwHighDateTime = m_Parser.Get<DWORD>();
			prop.Value.ft.dwLowDateTime = m_Parser.Get<DWORD>();
			break;
		case PT_BINARY:
			prop.Value.bin.cb = m_Parser.GetBlock<WORD>();
			// Note that we're not placing a restriction on how large a binary property we can parse. May need to revisit this.
			prop.Value.bin.lpb = m_Parser.GetBlockBYTES(prop.Value.bin.cb);
			break;
		case PT_MV_STRING8:
			prop.Value.MVszA.cValues = m_Parser.GetBlock<WORD>();
			if (prop.Value.MVszA.cValues)
			{
				for (ULONG j = 0; j < prop.Value.MVszA.cValues; j++)
				{
					prop.Value.MVszA.lppszA.emplace_back(m_Parser.GetBlockStringA());
				}
			}
			break;
		case PT_MV_UNICODE:
			prop.Value.MVszW.cValues = m_Parser.GetBlock<WORD>();
			if (prop.Value.MVszW.cValues)
			{
				for (ULONG j = 0; j < prop.Value.MVszW.cValues; j++)
				{
					prop.Value.MVszW.lppszW.emplace_back(m_Parser.GetBlockStringW());
				}
			}
			break;
		default:
			break;
		}

		if (ulCurrOffset == m_Parser.GetCurrentOffset())
		{
			// We didn't advance at all - we should return nothing.
			return {};
		}

		return prop;
	}

	_Check_return_ std::wstring PropsToString(DWORD PropCount, LPSPropValue Prop)
	{
		std::vector<std::wstring> property;

		if (Prop)
		{
			for (DWORD i = 0; i < PropCount; i++)
			{
				std::wstring PropString;
				std::wstring AltPropString;

				property.push_back(strings::formatmessage(IDS_PROPERTYDATAHEADER, i, Prop[i].ulPropTag));

				auto propTagNames = interpretprop::PropTagToPropName(Prop[i].ulPropTag, false);
				if (!propTagNames.bestGuess.empty())
				{
					property.push_back(strings::formatmessage(IDS_PROPERTYDATANAME, propTagNames.bestGuess.c_str()));
				}

				if (!propTagNames.otherMatches.empty())
				{
					property.push_back(
						strings::formatmessage(IDS_PROPERTYDATAPARTIALMATCHES, propTagNames.otherMatches.c_str()));
				}

				interpretprop::InterpretProp(&Prop[i], &PropString, &AltPropString);
				property.push_back(strings::RemoveInvalidCharactersW(
					strings::formatmessage(IDS_PROPERTYDATA, PropString.c_str(), AltPropString.c_str()), false));

				auto szSmartView = InterpretPropSmartView(&Prop[i], nullptr, nullptr, nullptr, false, false);

				if (!szSmartView.empty())
				{
					property.push_back(strings::formatmessage(IDS_PROPERTYDATASMARTVIEW, szSmartView.c_str()));
				}
			}
		}

		return strings::join(property, L"\r\n");
	}
}