#include <core/stdafx.h>
#include <core/mapi/cache/namedPropCache.h>
#include <core/mapi/cache/namedPropCacheEntry.h>
#include <core/interpret/guid.h>
#include <core/mapi/mapiMemory.h>
#include <core/utility/registry.h>
#include <core/utility/strings.h>
#include <core/utility/output.h>
#include <core/addin/mfcmapi.h>
#include <core/addin/addin.h>
#include <core/utility/error.h>

namespace oldcache
{
	ULONG cbPropName(LPCWSTR lpwstrName) noexcept
	{
		// lpwstrName is LPWSTR which means it's ALWAYS unicode
		// But some folks get it wrong and stuff ANSI data in there
		// So we check the string length both ways to make our best guess
		const auto cchShortLen = strnlen_s(reinterpret_cast<LPCSTR>(lpwstrName), RSIZE_MAX);
		const auto cchWideLen = wcsnlen_s(lpwstrName, RSIZE_MAX);
		auto cbName = ULONG();

		if (cchShortLen < cchWideLen)
		{
			// this is the *proper* case
			cbName = (cchWideLen + 1) * sizeof WCHAR;
		}
		else
		{
			// This is the case where ANSI data was shoved into a unicode string.
			// Add a couple extra NULL in case we read this as unicode again.
			cbName = (cchShortLen + 3) * sizeof CHAR;
		}

		return cbName;
	}

	// Go through all the details of copying allocated data from a MAPINAMEID
	MAPINAMEID* CopyMapiNameId(
		const MAPINAMEID& src,
		_In_ LPVOID lpMAPIParent) // Allocate using MAPI with this as a parent
	{
		const auto dst = mapi::allocate<LPMAPINAMEID>(sizeof(MAPINAMEID), lpMAPIParent);
		if (dst)
		{
			dst->lpguid = nullptr;
			dst->Kind.lID = MNID_ID;

			if (src.lpguid)
			{
				dst->lpguid = mapi::allocate<LPGUID>(sizeof(GUID), lpMAPIParent);
				if (dst->lpguid)
				{
					memcpy(dst->lpguid, src.lpguid, sizeof GUID);
				}
			}

			dst->ulKind = src.ulKind;
			if (MNID_ID == src.ulKind)
			{
				dst->Kind.lID = src.Kind.lID;
			}
			else if (MNID_STRING == src.ulKind)
			{
				if (src.Kind.lpwstrName)
				{
					const auto cbName = cbPropName(src.Kind.lpwstrName);

					dst->Kind.lpwstrName = mapi::allocate<LPWSTR>(cbName, lpMAPIParent);
					if (dst->Kind.lpwstrName)
					{
						memcpy(dst->Kind.lpwstrName, src.Kind.lpwstrName, cbName);
					}
				}
			}
		}

		return dst;
	}

	class namedPropCache
	{
	public:
		static std::list<std::shared_ptr<NamedPropCacheEntry>>& getCache() noexcept
		{
			// We keep a list of named prop cache entries
			static std::list<std::shared_ptr<NamedPropCacheEntry>> cache;
			return cache;
		}

		_Check_return_ static std::shared_ptr<NamedPropCacheEntry>
		FindCacheEntry(const std::function<bool(const std::shared_ptr<NamedPropCacheEntry>&)>& compare) noexcept
		{
			const auto& cache = getCache();
			const auto entry = find_if(cache.begin(), cache.end(), [compare](const auto& namedPropCacheEntry) noexcept {
				return compare(namedPropCacheEntry);
			});

			return entry != cache.end() ? *entry : nullptr;
		}

		static void AddMapping(
			ULONG cbSig, // Count bytes of signature
			_In_opt_count_(cbSig) LPBYTE lpSig, // Signature
			ULONG ulNumProps, // Number of mapped names
			_In_count_(ulNumProps) LPMAPINAMEID* lppPropNames, // Output from GetNamesFromIDs, input for GetIDsFromNames
			_In_ const SPropTagArray* lpTag) // Input for GetNamesFromIDs, output from GetIDsFromNames
		{
			if (!ulNumProps || !lppPropNames || !lpTag) return;
			if (ulNumProps != lpTag->cValues) return; // Wouldn't know what to do with this

			auto& cache = getCache();
			for (ULONG ulSource = 0; ulSource < ulNumProps; ulSource++)
			{
				if (lppPropNames[ulSource])
				{
					if (fIsSet(output::dbgLevel::NamedPropCacheMisses) && lppPropNames[ulSource]->ulKind == MNID_ID)
					{
						auto names = NameIDToPropNames(lppPropNames[ulSource]);
						if (names.empty())
						{
							output::DebugPrint(
								output::dbgLevel::NamedPropCacheMisses,
								L"AddMapping: Caching unknown property 0x%08X %ws\n",
								lppPropNames[ulSource]->Kind.lID,
								guid::GUIDToStringAndName(lppPropNames[ulSource]->lpguid).c_str());
						}
					}

					cache.emplace_back(std::make_shared<NamedPropCacheEntry>(
						cbSig, lpSig, lppPropNames[ulSource], PROP_ID(lpTag->aulPropTag[ulSource])));
				}
			}
		}

		// Add to the cache entries that don't have a mapping signature
		// For each one, we have to check that the item isn't already in the cache
		// Since this function should rarely be hit, we'll do it the slow but easy way...
		// One entry at a time
		static void AddMappingWithoutSignature(
			ULONG ulNumProps, // Number of mapped names
			_In_count_(ulNumProps) LPMAPINAMEID* lppPropNames, // Output from GetNamesFromIDs, input for GetIDsFromNames
			_In_ const SPropTagArray* lpTag) // Input for GetNamesFromIDs, output from GetIDsFromNames
		{
			if (!ulNumProps || !lppPropNames || !lpTag) return;
			if (ulNumProps != lpTag->cValues) return; // Wouldn't know what to do with this

			SPropTagArray sptaTag = {0};
			sptaTag.cValues = 1;
			for (ULONG ulProp = 0; ulProp < ulNumProps; ulProp++)
			{
				if (lppPropNames[ulProp])
				{
					const auto lpNamedPropCacheEntry = FindCacheEntry([&](const auto& entry) noexcept {
						return entry->match(
							PROP_ID(lpTag->aulPropTag[ulProp]),
							lppPropNames[ulProp]->lpguid,
							lppPropNames[ulProp]->ulKind,
							lppPropNames[ulProp]->Kind.lID,
							lppPropNames[ulProp]->Kind.lpwstrName);
					});
					if (!lpNamedPropCacheEntry)
					{
						sptaTag.aulPropTag[0] = lpTag->aulPropTag[ulProp];
						AddMapping(0, nullptr, 1, &lppPropNames[ulProp], &sptaTag);
					}
				}
			}
		}

		_Check_return_ static HRESULT CacheGetNamesFromIDs(
			_In_ LPMAPIPROP lpMAPIProp,
			ULONG cbSig,
			_In_count_(cbSig) LPBYTE lpSig,
			_In_ LPSPropTagArray* lppPropTags,
			_Out_ ULONG* lpcPropNames,
			_Out_ _Deref_post_opt_cap_(*lpcPropNames) LPMAPINAMEID** lpppPropNames)
		{
			if (lpppPropNames) *lpppPropNames = {};
			if (!lpMAPIProp || !lppPropTags || !*lppPropTags || !cbSig || !lpSig) return MAPI_E_INVALID_PARAMETER;

			// We're going to walk the cache, looking for the values we need. As soon as we have all the values we need, we're done
			// If we reach the end of the cache and don't have everything, we set up to make a GetNamesFromIDs call.

			auto hRes = S_OK;
			const auto lpPropTags = *lppPropTags;
			*lpcPropNames = 0;
			// First, allocate our results using MAPI
			const auto lppNameIDs = mapi::allocate<LPMAPINAMEID*>(sizeof(MAPINAMEID*) * lpPropTags->cValues);
			if (lppNameIDs)
			{
				// Assume we'll miss on everything
				auto ulMisses = lpPropTags->cValues;

				// For each tag we wish to look up...
				for (ULONG ulTarget = 0; ulTarget < lpPropTags->cValues; ulTarget++)
				{
					// ...check the cache
					const auto lpEntry = FindCacheEntry([&](const auto& entry) noexcept {
						return entry->match(cbSig, lpSig, PROP_ID(lpPropTags->aulPropTag[ulTarget]));
					});

					if (lpEntry)
					{
						// We have a hit - copy the data over
						lppNameIDs[ulTarget] = const_cast<MAPINAMEID*>(lpEntry->getMapiNameId());

						// Got a hit, decrement the miss counter
						ulMisses--;
					}
				}

				// Go to MAPI with whatever's left. We set up for a single call to GetNamesFromIDs.
				if (0 != ulMisses)
				{
					auto lpUncachedTags = mapi::allocate<LPSPropTagArray>(CbNewSPropTagArray(ulMisses));
					if (lpUncachedTags)
					{
						lpUncachedTags->cValues = ulMisses;
						ULONG ulUncachedTag = NULL;
						for (ULONG ulTarget = 0; ulTarget < lpPropTags->cValues; ulTarget++)
						{
							// We're looking for any result which doesn't have a mapping
							if (!lppNameIDs[ulTarget])
							{
								lpUncachedTags->aulPropTag[ulUncachedTag] = lpPropTags->aulPropTag[ulTarget];
								ulUncachedTag++;
							}
						}

						ULONG ulUncachedPropNames = 0;
						LPMAPINAMEID* lppUncachedPropNames = nullptr;

						hRes = WC_H_GETPROPS(lpMAPIProp->GetNamesFromIDs(
							&lpUncachedTags, nullptr, NULL, &ulUncachedPropNames, &lppUncachedPropNames));
						if (SUCCEEDED(hRes) && ulUncachedPropNames == ulMisses && lppUncachedPropNames)
						{
							// Cache the results
							AddMapping(cbSig, lpSig, ulUncachedPropNames, lppUncachedPropNames, lpUncachedTags);

							// Copy our results over
							// Loop over the target array, looking for empty slots
							ulUncachedTag = 0;
							for (ULONG ulTarget = 0; ulTarget < lpPropTags->cValues; ulTarget++)
							{
								// Found an empty slot
								if (!lppNameIDs[ulTarget])
								{
									// copy the next result into it
									if (lppUncachedPropNames[ulUncachedTag])
									{
										lppNameIDs[ulTarget] =
											CopyMapiNameId(*lppUncachedPropNames[ulUncachedTag], lppNameIDs);

										// Got a hit, decrement the miss counter
										ulMisses--;
									}

									// Whether we copied or not, move on to the next one
									ulUncachedTag++;
								}
							}
						}

						MAPIFreeBuffer(lppUncachedPropNames);
					}

					MAPIFreeBuffer(lpUncachedTags);

					if (ulMisses != 0) hRes = MAPI_W_ERRORS_RETURNED;
				}

				*lpppPropNames = lppNameIDs;
				if (lpcPropNames) *lpcPropNames = lpPropTags->cValues;
			}

			return hRes;
		}
	};

	// GetNamesFromIDs without a mapping signature
	_Check_return_ HRESULT GetNamesFromIDs(
		_In_ LPMAPIPROP lpMAPIProp,
		_In_ LPSPropTagArray* lppPropTags,
		_In_opt_ LPGUID lpPropSetGuid,
		ULONG ulFlags,
		_Out_ ULONG* lpcPropNames,
		_Out_ _Deref_post_cap_(*lpcPropNames) LPMAPINAMEID** lpppPropNames)
	{
		return GetNamesFromIDs(lpMAPIProp, nullptr, lppPropTags, lpPropSetGuid, ulFlags, lpcPropNames, lpppPropNames);
	}

	_Check_return_ HRESULT GetNamesFromIDs(
		_In_ LPMAPIPROP lpMAPIProp,
		_In_opt_ const _SBinary* lpMappingSignature,
		_In_ LPSPropTagArray* lppPropTags,
		_In_opt_ LPGUID lpPropSetGuid,
		ULONG ulFlags,
		_Out_ ULONG* lpcPropNames,
		_Out_ _Deref_post_cap_(*lpcPropNames) LPMAPINAMEID** lpppPropNames)
	{
		if (!lpMAPIProp) return MAPI_E_INVALID_PARAMETER;

		*lpcPropNames = 0;
		// Check if we're bypassing the cache:
		if (!registry::cacheNamedProps ||
			// Assume an array was passed - none of my calling code passes a NULL tag array
			!lppPropTags || !*lppPropTags ||
			// None of my code uses these flags, but bypass the cache if we see them
			ulFlags || lpPropSetGuid)
		{
			return lpMAPIProp->GetNamesFromIDs(lppPropTags, lpPropSetGuid, ulFlags, lpcPropNames, lpppPropNames);
		}

		auto hRes = S_OK;
		LPSPropValue lpProp = nullptr;

		if (!lpMappingSignature)
		{
			// This error is too chatty to log - ignore it.
			hRes = HrGetOneProp(lpMAPIProp, PR_MAPPING_SIGNATURE, &lpProp);

			if (SUCCEEDED(hRes) && lpProp && PT_BINARY == PROP_TYPE(lpProp->ulPropTag))
			{
				lpMappingSignature = &lpProp->Value.bin;
			}
		}

		if (lpMappingSignature)
		{
			hRes = WC_H_GETPROPS(namedPropCache::CacheGetNamesFromIDs(
				lpMAPIProp, lpMappingSignature->cb, lpMappingSignature->lpb, lppPropTags, lpcPropNames, lpppPropNames));
		}
		else
		{
			hRes = WC_H_GETPROPS(
				lpMAPIProp->GetNamesFromIDs(lppPropTags, lpPropSetGuid, ulFlags, lpcPropNames, lpppPropNames));
			// Cache the results
			if (SUCCEEDED(hRes))
			{
				namedPropCache::AddMappingWithoutSignature(*lpcPropNames, *lpppPropNames, *lppPropTags);
			}
		}

		MAPIFreeBuffer(lpProp);
		return hRes;
	}

	_Check_return_ LPSPropTagArray CacheGetIDsFromNames(
		_In_ LPMAPIPROP lpMAPIProp,
		ULONG cbSig,
		_In_count_(cbSig) LPBYTE lpSig,
		ULONG cPropNames,
		_In_count_(cPropNames) LPMAPINAMEID* lppPropNames,
		ULONG ulFlags)
	{
		if (!lpMAPIProp || !cPropNames || !*lppPropNames) return nullptr;

		// We're going to walk the cache, looking for the values we need. As soon as we have all the values we need, we're done
		// If we reach the end of the cache and don't have everything, we set up to make a GetIDsFromNames call.

		// First, allocate our results using MAPI
		const auto lpPropTags = mapi::allocate<LPSPropTagArray>(CbNewSPropTagArray(cPropNames));
		if (lpPropTags)
		{
			lpPropTags->cValues = cPropNames;

			// Assume we'll miss on everything
			auto ulMisses = cPropNames;

			// For each tag we wish to look up...
			for (ULONG ulTarget = 0; ulTarget < cPropNames; ulTarget++)
			{
				// ...check the cache
				const auto lpEntry = namedPropCache::FindCacheEntry([&](const auto& entry) noexcept {
					return entry->match(
						cbSig,
						lpSig,
						lppPropNames[ulTarget]->lpguid,
						lppPropNames[ulTarget]->ulKind,
						lppPropNames[ulTarget]->Kind.lID,
						lppPropNames[ulTarget]->Kind.lpwstrName);
				});

				if (lpEntry)
				{
					// We have a hit - copy the data over
					lpPropTags->aulPropTag[ulTarget] = PROP_TAG(PT_UNSPECIFIED, lpEntry->getPropID());

					// Got a hit, decrement the miss counter
					ulMisses--;
				}
			}

			// Go to MAPI with whatever's left. We set up for a single call to GetIDsFromNames.
			if (0 != ulMisses)
			{
				auto lppUncachedPropNames = mapi::allocate<LPMAPINAMEID*>(sizeof(LPMAPINAMEID) * ulMisses);
				if (lppUncachedPropNames)
				{
					ULONG ulUncachedName = NULL;
					for (ULONG ulTarget = 0; ulTarget < cPropNames; ulTarget++)
					{
						// We're looking for any result which doesn't have a mapping
						if (!lpPropTags->aulPropTag[ulTarget])
						{
							// We don't need to reallocate everything - just match the pointers so we can make the call
							lppUncachedPropNames[ulUncachedName] = lppPropNames[ulTarget];
							ulUncachedName++;
						}
					}

					LPSPropTagArray lpUncachedTags = nullptr;

					EC_H_GETPROPS_S(
						lpMAPIProp->GetIDsFromNames(ulMisses, lppUncachedPropNames, ulFlags, &lpUncachedTags));
					if (lpUncachedTags && lpUncachedTags->cValues == ulMisses)
					{
						// Cache the results
						namedPropCache::AddMapping(
							cbSig, lpSig, lpUncachedTags->cValues, lppUncachedPropNames, lpUncachedTags);

						// Copy our results over
						// Loop over the target array, looking for empty slots
						// Each empty slot corresponds to one of our results, in order
						ulUncachedName = 0;
						for (ULONG ulTarget = 0; ulTarget < lpPropTags->cValues; ulTarget++)
						{
							// Found an empty slot
							if (!lpPropTags->aulPropTag[ulTarget])
							{
								lpPropTags->aulPropTag[ulTarget] = lpUncachedTags->aulPropTag[ulUncachedName];

								// If we got a hit, decrement the miss counter
								// But only if the hit wasn't an error
								if (lpPropTags->aulPropTag[ulTarget] && PT_ERROR != lpPropTags->aulPropTag[ulTarget])
								{
									ulMisses--;
								}

								// Move on to the next uncached name
								ulUncachedName++;
							}
						}
					}

					MAPIFreeBuffer(lpUncachedTags);
				}

				MAPIFreeBuffer(lppUncachedPropNames);
			}
		}

		return lpPropTags;
	}

	_Check_return_ LPSPropTagArray GetIDsFromNames(
		_In_ LPMAPIPROP lpMAPIProp,
		ULONG cPropNames,
		_In_opt_count_(cPropNames) LPMAPINAMEID* lppPropNames,
		ULONG ulFlags)
	{
		if (!lpMAPIProp) return nullptr;

		auto propTags = LPSPropTagArray{};
		// Check if we're bypassing the cache:
		if (!registry::cacheNamedProps ||
			// If no names were passed, we have to bypass the cache
			// Should we cache results?
			!cPropNames || !lppPropNames || !*lppPropNames)
		{
			WC_H_GETPROPS_S(lpMAPIProp->GetIDsFromNames(cPropNames, lppPropNames, ulFlags, &propTags));
			return propTags;
		}

		LPSPropValue lpProp = nullptr;

		WC_MAPI_S(HrGetOneProp(lpMAPIProp, PR_MAPPING_SIGNATURE, &lpProp));

		if (lpProp && PT_BINARY == PROP_TYPE(lpProp->ulPropTag))
		{
			propTags = CacheGetIDsFromNames(
				lpMAPIProp, lpProp->Value.bin.cb, lpProp->Value.bin.lpb, cPropNames, lppPropNames, ulFlags);
		}
		else
		{
			WC_H_GETPROPS_S(lpMAPIProp->GetIDsFromNames(cPropNames, lppPropNames, ulFlags, &propTags));
			// Cache the results
			if (propTags)
			{
				namedPropCache::AddMappingWithoutSignature(cPropNames, lppPropNames, propTags);
			}
		}

		MAPIFreeBuffer(lpProp);

		return propTags;
	}

	// TagToString will prepend the http://schemas.microsoft.com/MAPI/ for us since it's a constant
	// We don't compute a DASL string for non-named props as FormatMessage in TagToString can handle those
	NamePropNames NameIDToStrings(_In_ LPMAPINAMEID lpNameID, ULONG ulPropTag)
	{
		NamePropNames namePropNames;

		// Can't generate strings without a MAPINAMEID structure
		if (!lpNameID) return {};

		auto lpNamedPropCacheEntry = std::shared_ptr<NamedPropCacheEntry>{};

		// If we're using the cache, look up the answer there and return
		if (registry::cacheNamedProps)
		{
			lpNamedPropCacheEntry = namedPropCache::FindCacheEntry([&](const auto& entry) noexcept {
				return entry->match(
					PROP_ID(ulPropTag),
					lpNameID->lpguid,
					lpNameID->ulKind,
					lpNameID->Kind.lID,
					lpNameID->Kind.lpwstrName);
			});
			if (lpNamedPropCacheEntry && lpNamedPropCacheEntry->hasCachedStrings())
			{
				return lpNamedPropCacheEntry->getNamePropNames();
			}

			// We shouldn't ever get here without a cached entry
			if (!lpNamedPropCacheEntry)
			{
				output::DebugPrint(
					output::dbgLevel::NamedProp,
					L"NameIDToStrings: Failed to find cache entry for ulPropTag = 0x%08X\n",
					ulPropTag);
				return namePropNames;
			}
		}

		output::DebugPrint(output::dbgLevel::NamedProp, L"Parsing named property\n");
		output::DebugPrint(output::dbgLevel::NamedProp, L"ulPropTag = 0x%08x\n", ulPropTag);
		namePropNames.guid = guid::GUIDToStringAndName(lpNameID->lpguid);
		output::DebugPrint(output::dbgLevel::NamedProp, L"lpNameID->lpguid = %ws\n", namePropNames.guid.c_str());

		auto szDASLGuid = guid::GUIDToString(lpNameID->lpguid);

		if (lpNameID->ulKind == MNID_ID)
		{
			output::DebugPrint(
				output::dbgLevel::NamedProp,
				L"lpNameID->Kind.lID = 0x%04X = %d\n",
				lpNameID->Kind.lID,
				lpNameID->Kind.lID);
			auto pidlids = NameIDToPropNames(lpNameID);

			if (!pidlids.empty())
			{
				namePropNames.bestPidLid = pidlids.front();
				pidlids.erase(pidlids.begin());
				namePropNames.otherPidLid = strings::join(pidlids, L", ");
				// Printing hex first gets a nice sort without spacing tricks
				namePropNames.name = strings::format(
					L"id: 0x%04X=%d = %ws", // STRING_OK
					lpNameID->Kind.lID,
					lpNameID->Kind.lID,
					namePropNames.bestPidLid.c_str());

				if (!namePropNames.otherPidLid.empty())
				{
					namePropNames.name += strings::format(L" (%ws)", namePropNames.otherPidLid.c_str());
				}
			}
			else
			{
				// Printing hex first gets a nice sort without spacing tricks
				namePropNames.name = strings::format(
					L"id: 0x%04X=%d", // STRING_OK
					lpNameID->Kind.lID,
					lpNameID->Kind.lID);
			}

			namePropNames.dasl = strings::format(
				L"id/%s/%04X%04X", // STRING_OK
				szDASLGuid.c_str(),
				lpNameID->Kind.lID,
				PROP_TYPE(ulPropTag));
		}
		else if (lpNameID->ulKind == MNID_STRING)
		{
			// lpwstrName is LPWSTR which means it's ALWAYS unicode
			// But some folks get it wrong and stuff ANSI data in there
			// So we check the string length both ways to make our best guess
			const auto cchShortLen = strnlen_s(reinterpret_cast<LPCSTR>(lpNameID->Kind.lpwstrName), RSIZE_MAX);
			const auto cchWideLen = wcsnlen_s(lpNameID->Kind.lpwstrName, RSIZE_MAX);

			if (cchShortLen < cchWideLen)
			{
				// this is the *proper* case
				output::DebugPrint(
					output::dbgLevel::NamedProp, L"lpNameID->Kind.lpwstrName = \"%ws\"\n", lpNameID->Kind.lpwstrName);
				namePropNames.name = lpNameID->Kind.lpwstrName;

				namePropNames.dasl = strings::format(
					L"string/%ws/%ws", // STRING_OK
					szDASLGuid.c_str(),
					lpNameID->Kind.lpwstrName);
			}
			else
			{
				// this is the case where ANSI data was shoved into a unicode string.
				output::DebugPrint(
					output::dbgLevel::NamedProp,
					L"Warning: ANSI data was found in a unicode field. This is a bug on the part of the creator of "
					L"this named property\n");
				output::DebugPrint(
					output::dbgLevel::NamedProp,
					L"lpNameID->Kind.lpwstrName = \"%hs\"\n",
					reinterpret_cast<LPCSTR>(lpNameID->Kind.lpwstrName));

				auto szComment = strings::loadstring(IDS_NAMEWASANSI);
				namePropNames.name =
					strings::format(L"%hs %ws", reinterpret_cast<LPSTR>(lpNameID->Kind.lpwstrName), szComment.c_str());

				namePropNames.dasl = strings::format(
					L"string/%ws/%hs", // STRING_OK
					szDASLGuid.c_str(),
					LPSTR(lpNameID->Kind.lpwstrName));
			}
		}

		// We've built our strings - if we're caching, put them in the cache
		if (lpNamedPropCacheEntry)
		{
			lpNamedPropCacheEntry->setNamePropNames(namePropNames);
		}

		return namePropNames;
	}

	NamePropNames NameIDToStrings(
		ULONG ulPropTag, // optional 'original' prop tag
		_In_opt_ LPMAPIPROP lpMAPIProp, // optional source object
		_In_opt_ LPMAPINAMEID lpNameID, // optional named property information to avoid GetNamesFromIDs call
		_In_opt_ const _SBinary*
			lpMappingSignature, // optional mapping signature for object to speed named prop lookups
		bool
			bIsAB) // true if we know we're dealing with an address book property (they can be > 8000 and not named props)
	{
		NamePropNames namePropNames;

		// Named Props
		LPMAPINAMEID* lppPropNames = nullptr;

		// If we weren't passed named property information and we need it, look it up
		// We check bIsAB here - some address book providers return garbage which will crash us
		if (!lpNameID && lpMAPIProp && // if we have an object
			!bIsAB && registry::parseNamedProps && // and we're parsing named props
			(registry::getPropNamesOnAllProps ||
			 PROP_ID(ulPropTag) >= 0x8000)) // and it's either a named prop or we're doing all props
		{
			SPropTagArray tag = {0};
			auto lpTag = &tag;
			ULONG ulPropNames = 0;
			tag.cValues = 1;
			tag.aulPropTag[0] = ulPropTag;

			const auto hRes = WC_H_GETPROPS(
				GetNamesFromIDs(lpMAPIProp, lpMappingSignature, &lpTag, nullptr, NULL, &ulPropNames, &lppPropNames));
			if (SUCCEEDED(hRes) && ulPropNames == 1 && lppPropNames && lppPropNames[0])
			{
				lpNameID = lppPropNames[0];
			}
		}

		if (lpNameID)
		{
			namePropNames = NameIDToStrings(lpNameID, ulPropTag);
		}

		// Avoid making the call if we don't have to so we don't accidently depend on MAPI
		if (lppPropNames) MAPIFreeBuffer(lppPropNames);

		return namePropNames;
	}

	// Returns string built from NameIDArray
	std::vector<std::wstring> NameIDToPropNames(_In_ const MAPINAMEID* lpNameID)
	{
		std::vector<std::wstring> results;
		if (!lpNameID) return {};
		if (lpNameID->ulKind != MNID_ID) return {};
		ULONG ulMatch = cache2::ulNoMatch;

		if (NameIDArray.empty()) return {};

		for (ULONG ulCur = 0; ulCur < NameIDArray.size(); ulCur++)
		{
			if (NameIDArray[ulCur].lValue == lpNameID->Kind.lID)
			{
				ulMatch = ulCur;
				break;
			}
		}

		if (cache2::ulNoMatch != ulMatch)
		{
			for (auto ulCur = ulMatch; ulCur < NameIDArray.size(); ulCur++)
			{
				if (NameIDArray[ulCur].lValue != lpNameID->Kind.lID) break;
				// We don't acknowledge array entries without guids
				if (!NameIDArray[ulCur].lpGuid) continue;
				// But if we weren't asked about a guid, we don't check one
				if (lpNameID->lpguid && !IsEqualGUID(*lpNameID->lpguid, *NameIDArray[ulCur].lpGuid)) continue;

				results.push_back(NameIDArray[ulCur].lpszName);
			}
		}

		return results;
	}
} // namespace oldcache