// SPDX-License-Identifier: Apache-2.0
#include "Data/CsvReader.h"

#include "Misc/FileHelper.h"

namespace Csv
{
bool ParseCells(const FString& Text, TArray<TArray<FString>>& OutRows, FString* OutError)
{
	OutRows.Reset();
	TArray<FString> Row;
	FString Cell;
	bool bInQuotes = false;
	bool bCellStarted = false;
	int32 Line = 1;

	const int32 Len = Text.Len();
	int32 I = 0;
	// Skip a UTF-8 BOM that survived decoding.
	if (Len > 0 && Text[0] == 0xFEFF) I = 1;

	auto EndRow = [&]() {
		Row.Add(MoveTemp(Cell));
		Cell.Reset();
		OutRows.Add(MoveTemp(Row));
		Row.Reset();
		bCellStarted = false;
	};

	for (; I < Len; ++I)
	{
		const TCHAR C = Text[I];
		if (bInQuotes)
		{
			if (C == TEXT('"'))
			{
				if (I + 1 < Len && Text[I + 1] == TEXT('"'))
				{
					Cell.AppendChar(TEXT('"'));
					++I;
				}
				else
				{
					bInQuotes = false;
				}
			}
			else
			{
				if (C == TEXT('\n')) ++Line;
				Cell.AppendChar(C);
			}
			continue;
		}
		switch (C)
		{
		case TEXT('"'):
			if (Cell.TrimStartAndEnd().IsEmpty())
			{
				Cell.Reset();
				bInQuotes = true;
				bCellStarted = true;
			}
			else
			{
				Cell.AppendChar(C); // stray quote inside an unquoted field: keep literally
			}
			break;
		case TEXT(','):
			Row.Add(MoveTemp(Cell));
			Cell.Reset();
			bCellStarted = true;
			break;
		case TEXT('\r'):
			break;
		case TEXT('\n'):
			EndRow();
			++Line;
			break;
		default:
			Cell.AppendChar(C);
			bCellStarted = true;
			break;
		}
	}
	if (bInQuotes)
	{
		if (OutError) *OutError = FString::Printf(TEXT("unterminated quoted field (started before line %d)"), Line);
		return false;
	}
	if (bCellStarted || !Cell.IsEmpty() || Row.Num() > 0) EndRow();
	return true;
}

bool ParseTable(const FString& Text, TArray<FRow>& OutRows, FString* OutError)
{
	OutRows.Reset();
	TArray<TArray<FString>> Cells;
	if (!ParseCells(Text, Cells, OutError)) return false;
	if (Cells.Num() == 0)
	{
		if (OutError) *OutError = TEXT("empty file");
		return false;
	}
	TArray<FString> Header;
	for (const FString& H : Cells[0]) Header.Add(H.TrimStartAndEnd().ToLower());

	for (int32 R = 1; R < Cells.Num(); ++R)
	{
		const TArray<FString>& Src = Cells[R];
		bool bBlank = true;
		for (const FString& V : Src)
		{
			if (!V.TrimStartAndEnd().IsEmpty())
			{
				bBlank = false;
				break;
			}
		}
		if (bBlank) continue;
		FRow Row;
		for (int32 C = 0; C < Header.Num(); ++C)
		{
			if (Header[C].IsEmpty()) continue;
			Row.Add(Header[C], C < Src.Num() ? Src[C].TrimStartAndEnd() : FString());
		}
		OutRows.Add(MoveTemp(Row));
	}
	return true;
}

bool LoadTable(const FString& Path, TArray<FRow>& OutRows, FString* OutError)
{
	FString Text;
	if (!FFileHelper::LoadFileToString(Text, *Path))
	{
		if (OutError) *OutError = FString::Printf(TEXT("cannot read %s"), *Path);
		return false;
	}
	return ParseTable(Text, OutRows, OutError);
}
} // namespace Csv
