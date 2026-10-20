// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "CoreMinimal.h"

/** RFC 4180 CSV parsing. Rows are maps keyed by lower-cased, trimmed header names. */
namespace Csv
{
using FRow = TMap<FString, FString>;

/** Raw parse into cells. Handles quoted fields, doubled quotes, CRLF and newlines inside quotes. */
CHESSSIMMARK_API bool ParseCells(const FString& Text, TArray<TArray<FString>>& OutRows, FString* OutError = nullptr);

/** Parse with a header row. Blank lines are skipped; values are trimmed. */
CHESSSIMMARK_API bool ParseTable(const FString& Text, TArray<FRow>& OutRows, FString* OutError = nullptr);

CHESSSIMMARK_API bool LoadTable(const FString& Path, TArray<FRow>& OutRows, FString* OutError = nullptr);

inline const FString& Get(const FRow& Row, const TCHAR* Column)
{
	static const FString Empty;
	const FString* V = Row.Find(Column);
	return V ? *V : Empty;
}
}
