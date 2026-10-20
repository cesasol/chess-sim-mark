// SPDX-License-Identifier: Apache-2.0
#include "ChessCore.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <sstream>

namespace chesscore
{

namespace
{
constexpr int KnightOffsets[8][2] = {{1, 2}, {2, 1}, {2, -1}, {1, -2}, {-1, -2}, {-2, -1}, {-2, 1}, {-1, 2}};
constexpr int KingOffsets[8][2] = {{1, 0}, {1, 1}, {0, 1}, {-1, 1}, {-1, 0}, {-1, -1}, {0, -1}, {1, -1}};
constexpr int BishopDirs[4][2] = {{1, 1}, {1, -1}, {-1, 1}, {-1, -1}};
constexpr int RookDirs[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};

bool OnBoard(int File, int Rank) { return File >= 0 && File < 8 && Rank >= 0 && Rank < 8; }

char PieceLetter(PieceType T)
{
	switch (T)
	{
	case PieceType::Pawn: return 'p';
	case PieceType::Knight: return 'n';
	case PieceType::Bishop: return 'b';
	case PieceType::Rook: return 'r';
	case PieceType::Queen: return 'q';
	case PieceType::King: return 'k';
	default: return '?';
	}
}

PieceType PieceFromLetter(char C)
{
	switch (std::tolower(static_cast<unsigned char>(C)))
	{
	case 'p': return PieceType::Pawn;
	case 'n': return PieceType::Knight;
	case 'b': return PieceType::Bishop;
	case 'r': return PieceType::Rook;
	case 'q': return PieceType::Queen;
	case 'k': return PieceType::King;
	default: return PieceType::None;
	}
}

std::vector<std::string> SplitWs(std::string_view S)
{
	std::vector<std::string> Out;
	std::string Cur;
	for (char C : S)
	{
		if (std::isspace(static_cast<unsigned char>(C)))
		{
			if (!Cur.empty()) { Out.push_back(Cur); Cur.clear(); }
		}
		else
		{
			Cur.push_back(C);
		}
	}
	if (!Cur.empty()) Out.push_back(Cur);
	return Out;
}

bool ParseInt(const std::string& S, int& Out)
{
	if (S.empty()) return false;
	int V = 0;
	for (char C : S)
	{
		if (!std::isdigit(static_cast<unsigned char>(C))) return false;
		V = V * 10 + (C - '0');
		if (V > 100000) return false;
	}
	Out = V;
	return true;
}
} // namespace

char Piece::ToFenChar() const
{
	if (Type == PieceType::None) return '.';
	const char L = PieceLetter(Type);
	return Side == Color::White ? static_cast<char>(std::toupper(L)) : L;
}

std::optional<Piece> Piece::FromFenChar(char C)
{
	const PieceType T = PieceFromLetter(C);
	if (T == PieceType::None) return std::nullopt;
	return Piece{T, std::isupper(static_cast<unsigned char>(C)) ? Color::White : Color::Black};
}

std::string SquareName(Square S)
{
	if (S < 0 || S > 63) return "-";
	std::string N;
	N.push_back(static_cast<char>('a' + FileOf(S)));
	N.push_back(static_cast<char>('1' + RankOf(S)));
	return N;
}

Square ParseSquare(std::string_view Name)
{
	if (Name.size() != 2) return NoSquare;
	const int F = Name[0] - 'a';
	const int R = Name[1] - '1';
	if (!OnBoard(F, R)) return NoSquare;
	return MakeSquare(F, R);
}

bool IsDraw(GameStatus S)
{
	return S == GameStatus::Stalemate || S == GameStatus::DrawFiftyMove || S == GameStatus::DrawThreefold ||
		S == GameStatus::DrawInsufficientMaterial;
}

std::string ChessMove::ToUci() const
{
	if (!IsValid()) return "0000";
	std::string S = SquareName(From) + SquareName(To);
	if (Promotion != PieceType::None) S.push_back(PieceLetter(Promotion));
	return S;
}

std::optional<ChessMove> ChessMove::FromUciString(std::string_view Uci)
{
	if (Uci.size() != 4 && Uci.size() != 5) return std::nullopt;
	ChessMove M;
	M.From = ParseSquare(Uci.substr(0, 2));
	M.To = ParseSquare(Uci.substr(2, 2));
	if (M.From == NoSquare || M.To == NoSquare) return std::nullopt;
	if (Uci.size() == 5)
	{
		M.Promotion = PieceFromLetter(Uci[4]);
		if (M.Promotion == PieceType::None || M.Promotion == PieceType::Pawn || M.Promotion == PieceType::King)
			return std::nullopt;
	}
	return M;
}

// ---------------------------------------------------------------------------
// Position

Position::Position() = default;

Position Position::Start() { return *FromFen(StartFen); }

std::optional<Position> Position::FromFen(std::string_view Fen)
{
	const std::vector<std::string> Parts = SplitWs(Fen);
	if (Parts.size() < 4) return std::nullopt;

	Position P;
	int Rank = 7, File = 0;
	for (char C : Parts[0])
	{
		if (C == '/')
		{
			if (File != 8) return std::nullopt;
			--Rank;
			File = 0;
			if (Rank < 0) return std::nullopt;
		}
		else if (C >= '1' && C <= '8')
		{
			File += C - '0';
			if (File > 8) return std::nullopt;
		}
		else
		{
			const std::optional<Piece> Pc = Piece::FromFenChar(C);
			if (!Pc || File > 7) return std::nullopt;
			P.Board[MakeSquare(File, Rank)] = *Pc;
			++File;
		}
	}
	if (Rank != 0 || File != 8) return std::nullopt;

	if (Parts[1] == "w") P.Side = Color::White;
	else if (Parts[1] == "b") P.Side = Color::Black;
	else return std::nullopt;

	P.CastleRights = CastleNone;
	if (Parts[2] != "-")
	{
		for (char C : Parts[2])
		{
			switch (C)
			{
			case 'K': P.CastleRights |= CastleWK; break;
			case 'Q': P.CastleRights |= CastleWQ; break;
			case 'k': P.CastleRights |= CastleBK; break;
			case 'q': P.CastleRights |= CastleBQ; break;
			default: return std::nullopt;
			}
		}
	}

	P.EpSquare = Parts[3] == "-" ? NoSquare : ParseSquare(Parts[3]);
	if (Parts[3] != "-" && P.EpSquare == NoSquare) return std::nullopt;

	if (Parts.size() > 4 && !ParseInt(Parts[4], P.Halfmove)) return std::nullopt;
	if (Parts.size() > 5 && !ParseInt(Parts[5], P.Fullmove)) return std::nullopt;

	// Exactly one king per side is required for a playable position.
	int WK = 0, BK = 0;
	for (const Piece& Pc : P.Board)
	{
		if (Pc.Is(Color::White, PieceType::King)) ++WK;
		if (Pc.Is(Color::Black, PieceType::King)) ++BK;
	}
	if (WK != 1 || BK != 1) return std::nullopt;
	return P;
}

std::string Position::RepetitionKey() const
{
	std::string S;
	for (int Rank = 7; Rank >= 0; --Rank)
	{
		int Empty = 0;
		for (int File = 0; File < 8; ++File)
		{
			const Piece& Pc = Board[MakeSquare(File, Rank)];
			if (Pc.IsEmpty()) { ++Empty; continue; }
			if (Empty) { S.push_back(static_cast<char>('0' + Empty)); Empty = 0; }
			S.push_back(Pc.ToFenChar());
		}
		if (Empty) S.push_back(static_cast<char>('0' + Empty));
		if (Rank > 0) S.push_back('/');
	}
	S += Side == Color::White ? " w " : " b ";
	if (CastleRights == CastleNone) S += "-";
	else
	{
		if (CastleRights & CastleWK) S += "K";
		if (CastleRights & CastleWQ) S += "Q";
		if (CastleRights & CastleBK) S += "k";
		if (CastleRights & CastleBQ) S += "q";
	}
	S += " ";
	// Only record the en-passant square when a capture there is actually possible,
	// so positions compare equal for repetition purposes.
	bool EpRelevant = false;
	if (EpSquare != NoSquare)
	{
		const int Dir = Side == Color::White ? -1 : 1;
		for (int Df : {-1, 1})
		{
			const int F = FileOf(EpSquare) + Df, R = RankOf(EpSquare) + Dir;
			if (OnBoard(F, R) && Board[MakeSquare(F, R)].Is(Side, PieceType::Pawn)) EpRelevant = true;
		}
	}
	S += EpRelevant ? SquareName(EpSquare) : "-";
	return S;
}

std::string Position::ToFen() const
{
	std::string S;
	for (int Rank = 7; Rank >= 0; --Rank)
	{
		int Empty = 0;
		for (int File = 0; File < 8; ++File)
		{
			const Piece& Pc = Board[MakeSquare(File, Rank)];
			if (Pc.IsEmpty()) { ++Empty; continue; }
			if (Empty) { S.push_back(static_cast<char>('0' + Empty)); Empty = 0; }
			S.push_back(Pc.ToFenChar());
		}
		if (Empty) S.push_back(static_cast<char>('0' + Empty));
		if (Rank > 0) S.push_back('/');
	}
	S += Side == Color::White ? " w " : " b ";
	if (CastleRights == CastleNone) S += "-";
	else
	{
		if (CastleRights & CastleWK) S += "K";
		if (CastleRights & CastleWQ) S += "Q";
		if (CastleRights & CastleBK) S += "k";
		if (CastleRights & CastleBQ) S += "q";
	}
	S += " " + SquareName(EpSquare);
	S += " " + std::to_string(Halfmove) + " " + std::to_string(Fullmove);
	return S;
}

Square Position::KingSquare(Color C) const
{
	for (Square S = 0; S < 64; ++S)
		if (Board[S].Is(C, PieceType::King)) return S;
	return NoSquare;
}

bool Position::IsSquareAttacked(Square Sq, Color By) const
{
	const int F = FileOf(Sq), R = RankOf(Sq);

	// Pawns: a pawn of colour By attacks diagonally forward.
	const int PawnRank = By == Color::White ? R - 1 : R + 1;
	for (int Df : {-1, 1})
		if (OnBoard(F + Df, PawnRank) && Board[MakeSquare(F + Df, PawnRank)].Is(By, PieceType::Pawn)) return true;

	for (const auto& O : KnightOffsets)
		if (OnBoard(F + O[0], R + O[1]) && Board[MakeSquare(F + O[0], R + O[1])].Is(By, PieceType::Knight)) return true;

	for (const auto& O : KingOffsets)
		if (OnBoard(F + O[0], R + O[1]) && Board[MakeSquare(F + O[0], R + O[1])].Is(By, PieceType::King)) return true;

	auto Slide = [&](const int (*Dirs)[2], PieceType A, PieceType B) {
		for (int I = 0; I < 4; ++I)
		{
			int Cf = F + Dirs[I][0], Cr = R + Dirs[I][1];
			while (OnBoard(Cf, Cr))
			{
				const Piece& Pc = Board[MakeSquare(Cf, Cr)];
				if (!Pc.IsEmpty())
				{
					if (Pc.Side == By && (Pc.Type == A || Pc.Type == B)) return true;
					break;
				}
				Cf += Dirs[I][0];
				Cr += Dirs[I][1];
			}
		}
		return false;
	};
	return Slide(BishopDirs, PieceType::Bishop, PieceType::Queen) || Slide(RookDirs, PieceType::Rook, PieceType::Queen);
}

bool Position::IsKingAttacked(Color C) const
{
	const Square K = KingSquare(C);
	return K != NoSquare && IsSquareAttacked(K, Opposite(C));
}

void Position::GeneratePseudoLegal(std::vector<ChessMove>& Out) const
{
	const Color Us = Side, Them = Opposite(Side);
	for (Square S = 0; S < 64; ++S)
	{
		const Piece& Pc = Board[S];
		if (Pc.IsEmpty() || Pc.Side != Us) continue;
		const int F = FileOf(S), R = RankOf(S);

		auto AddTo = [&](int Tf, int Tr) {
			Out.push_back(ChessMove{S, MakeSquare(Tf, Tr), PieceType::None});
		};

		switch (Pc.Type)
		{
		case PieceType::Pawn:
		{
			const int Dir = Us == Color::White ? 1 : -1;
			const int StartRank = Us == Color::White ? 1 : 6;
			const int PromoRank = Us == Color::White ? 7 : 0;
			auto AddPawn = [&](int Tf, int Tr) {
				if (Tr == PromoRank)
				{
					for (PieceType P : {PieceType::Queen, PieceType::Rook, PieceType::Bishop, PieceType::Knight})
						Out.push_back(ChessMove{S, MakeSquare(Tf, Tr), P});
				}
				else
				{
					AddTo(Tf, Tr);
				}
			};
			if (OnBoard(F, R + Dir) && Board[MakeSquare(F, R + Dir)].IsEmpty())
			{
				AddPawn(F, R + Dir);
				if (R == StartRank && Board[MakeSquare(F, R + 2 * Dir)].IsEmpty()) AddTo(F, R + 2 * Dir);
			}
			for (int Df : {-1, 1})
			{
				const int Tf = F + Df, Tr = R + Dir;
				if (!OnBoard(Tf, Tr)) continue;
				const Square T = MakeSquare(Tf, Tr);
				if ((!Board[T].IsEmpty() && Board[T].Side == Them) || T == EpSquare) AddPawn(Tf, Tr);
			}
			break;
		}
		case PieceType::Knight:
		case PieceType::King:
		{
			const auto& Offs = Pc.Type == PieceType::Knight ? KnightOffsets : KingOffsets;
			for (const auto& O : Offs)
			{
				const int Tf = F + O[0], Tr = R + O[1];
				if (!OnBoard(Tf, Tr)) continue;
				const Piece& Target = Board[MakeSquare(Tf, Tr)];
				if (Target.IsEmpty() || Target.Side == Them) AddTo(Tf, Tr);
			}
			if (Pc.Type == PieceType::King && !IsSquareAttacked(S, Them))
			{
				const int HomeRank = Us == Color::White ? 0 : 7;
				const uint8_t KS = Us == Color::White ? CastleWK : CastleBK;
				const uint8_t QS = Us == Color::White ? CastleWQ : CastleBQ;
				if (S == MakeSquare(4, HomeRank))
				{
					if ((CastleRights & KS) && Board[MakeSquare(5, HomeRank)].IsEmpty() &&
						Board[MakeSquare(6, HomeRank)].IsEmpty() && Board[MakeSquare(7, HomeRank)].Is(Us, PieceType::Rook) &&
						!IsSquareAttacked(MakeSquare(5, HomeRank), Them) && !IsSquareAttacked(MakeSquare(6, HomeRank), Them))
						AddTo(6, HomeRank);
					if ((CastleRights & QS) && Board[MakeSquare(3, HomeRank)].IsEmpty() &&
						Board[MakeSquare(2, HomeRank)].IsEmpty() && Board[MakeSquare(1, HomeRank)].IsEmpty() &&
						Board[MakeSquare(0, HomeRank)].Is(Us, PieceType::Rook) &&
						!IsSquareAttacked(MakeSquare(3, HomeRank), Them) && !IsSquareAttacked(MakeSquare(2, HomeRank), Them))
						AddTo(2, HomeRank);
				}
			}
			break;
		}
		case PieceType::Bishop:
		case PieceType::Rook:
		case PieceType::Queen:
		{
			auto Slide = [&](const int (*Dirs)[2]) {
				for (int I = 0; I < 4; ++I)
				{
					int Tf = F + Dirs[I][0], Tr = R + Dirs[I][1];
					while (OnBoard(Tf, Tr))
					{
						const Piece& Target = Board[MakeSquare(Tf, Tr)];
						if (Target.IsEmpty()) AddTo(Tf, Tr);
						else
						{
							if (Target.Side == Them) AddTo(Tf, Tr);
							break;
						}
						Tf += Dirs[I][0];
						Tr += Dirs[I][1];
					}
				}
			};
			if (Pc.Type != PieceType::Rook) Slide(BishopDirs);
			if (Pc.Type != PieceType::Bishop) Slide(RookDirs);
			break;
		}
		default: break;
		}
	}
}

std::vector<ChessMove> Position::LegalMoves() const
{
	std::vector<ChessMove> Pseudo;
	Pseudo.reserve(64);
	GeneratePseudoLegal(Pseudo);
	std::vector<ChessMove> Legal;
	Legal.reserve(Pseudo.size());
	for (const ChessMove& M : Pseudo)
	{
		if (!Apply(M).IsKingAttacked(Side)) Legal.push_back(M);
	}
	return Legal;
}

std::vector<ChessMove> Position::LegalMovesFrom(Square From) const
{
	std::vector<ChessMove> All = LegalMoves();
	std::vector<ChessMove> Out;
	for (const ChessMove& M : All)
		if (M.From == From) Out.push_back(M);
	return Out;
}

bool Position::IsLegal(const ChessMove& M) const
{
	const std::vector<ChessMove> All = LegalMoves();
	return std::find(All.begin(), All.end(), M) != All.end();
}

bool Position::IsCapture(const ChessMove& M) const
{
	if (!Board[M.To].IsEmpty()) return true;
	return Board[M.From].Type == PieceType::Pawn && M.To == EpSquare;
}

Position Position::Apply(const ChessMove& M) const
{
	Position P = *this;
	const Piece Moving = Board[M.From];
	const Color Us = Side;
	const bool Capture = IsCapture(M);

	P.Board[M.From] = Piece{};
	P.Board[M.To] = Moving;
	P.EpSquare = NoSquare;

	if (Moving.Type == PieceType::Pawn)
	{
		if (M.To == EpSquare)
		{
			// En passant: remove the pawn behind the target square.
			const int CapRank = RankOf(M.To) + (Us == Color::White ? -1 : 1);
			P.Board[MakeSquare(FileOf(M.To), CapRank)] = Piece{};
		}
		if (std::abs(RankOf(M.To) - RankOf(M.From)) == 2)
			P.EpSquare = MakeSquare(FileOf(M.From), (RankOf(M.From) + RankOf(M.To)) / 2);
		if (M.Promotion != PieceType::None) P.Board[M.To] = Piece{M.Promotion, Us};
	}
	else if (Moving.Type == PieceType::King && std::abs(FileOf(M.To) - FileOf(M.From)) == 2)
	{
		const int HomeRank = RankOf(M.From);
		if (FileOf(M.To) == 6)
		{
			P.Board[MakeSquare(5, HomeRank)] = P.Board[MakeSquare(7, HomeRank)];
			P.Board[MakeSquare(7, HomeRank)] = Piece{};
		}
		else
		{
			P.Board[MakeSquare(3, HomeRank)] = P.Board[MakeSquare(0, HomeRank)];
			P.Board[MakeSquare(0, HomeRank)] = Piece{};
		}
	}

	// Any move from or to a corner/king square revokes the matching rights.
	auto Revoke = [&](Square S) {
		if (S == MakeSquare(4, 0)) P.CastleRights &= ~(CastleWK | CastleWQ);
		if (S == MakeSquare(7, 0)) P.CastleRights &= ~CastleWK;
		if (S == MakeSquare(0, 0)) P.CastleRights &= ~CastleWQ;
		if (S == MakeSquare(4, 7)) P.CastleRights &= ~(CastleBK | CastleBQ);
		if (S == MakeSquare(7, 7)) P.CastleRights &= ~CastleBK;
		if (S == MakeSquare(0, 7)) P.CastleRights &= ~CastleBQ;
	};
	Revoke(M.From);
	Revoke(M.To);

	P.Halfmove = (Moving.Type == PieceType::Pawn || Capture) ? 0 : Halfmove + 1;
	if (Us == Color::Black) ++P.Fullmove;
	P.Side = Opposite(Us);
	return P;
}

bool Position::HasInsufficientMaterial() const
{
	// K vs K, K+minor vs K, and K+B vs K+B with same-coloured bishops.
	int Minors = 0;
	int BishopSquareColor = -1;
	bool BishopsMixed = false;
	int Knights = 0;
	for (Square S = 0; S < 64; ++S)
	{
		const Piece& Pc = Board[S];
		switch (Pc.Type)
		{
		case PieceType::None:
		case PieceType::King: break;
		case PieceType::Pawn:
		case PieceType::Rook:
		case PieceType::Queen: return false;
		case PieceType::Knight: ++Minors; ++Knights; break;
		case PieceType::Bishop:
		{
			++Minors;
			const int C = (FileOf(S) + RankOf(S)) & 1;
			if (BishopSquareColor == -1) BishopSquareColor = C;
			else if (BishopSquareColor != C) BishopsMixed = true;
			break;
		}
		}
	}
	if (Minors <= 1) return true;
	return Knights == 0 && !BishopsMixed;
}

std::string Position::ToSan(const ChessMove& M) const
{
	const Piece Moving = Board[M.From];
	std::string San;
	if (Moving.Type == PieceType::King && std::abs(FileOf(M.To) - FileOf(M.From)) == 2)
	{
		San = FileOf(M.To) == 6 ? "O-O" : "O-O-O";
	}
	else
	{
		const bool Capture = IsCapture(M);
		if (Moving.Type == PieceType::Pawn)
		{
			if (Capture) San.push_back(static_cast<char>('a' + FileOf(M.From)));
		}
		else
		{
			San.push_back(static_cast<char>(std::toupper(PieceLetter(Moving.Type))));
			// Disambiguation against other same-type pieces that can reach M.To.
			bool Ambiguous = false, SameFile = false, SameRank = false;
			for (const ChessMove& O : LegalMoves())
			{
				if (O.To != M.To || O.From == M.From || Board[O.From].Type != Moving.Type) continue;
				Ambiguous = true;
				if (FileOf(O.From) == FileOf(M.From)) SameFile = true;
				if (RankOf(O.From) == RankOf(M.From)) SameRank = true;
			}
			if (Ambiguous)
			{
				if (!SameFile) San.push_back(static_cast<char>('a' + FileOf(M.From)));
				else if (!SameRank) San.push_back(static_cast<char>('1' + RankOf(M.From)));
				else San += SquareName(M.From);
			}
		}
		if (Capture) San.push_back('x');
		San += SquareName(M.To);
		if (M.Promotion != PieceType::None)
		{
			San.push_back('=');
			San.push_back(static_cast<char>(std::toupper(PieceLetter(M.Promotion))));
		}
	}
	const Position Next = Apply(M);
	if (Next.InCheck()) San.push_back(Next.LegalMoves().empty() ? '#' : '+');
	return San;
}

std::optional<ChessMove> Position::ParseSan(std::string_view San) const
{
	std::string Clean;
	for (char C : San)
		if (C != '+' && C != '#' && C != '!' && C != '?') Clean.push_back(C);
	for (const ChessMove& M : LegalMoves())
	{
		std::string Candidate = ToSan(M);
		Candidate.erase(std::remove_if(Candidate.begin(), Candidate.end(), [](char C) { return C == '+' || C == '#'; }),
			Candidate.end());
		if (Candidate == Clean) return M;
	}
	return std::nullopt;
}

uint64_t Position::Perft(int Depth) const
{
	if (Depth <= 0) return 1;
	const std::vector<ChessMove> Moves = LegalMoves();
	if (Depth == 1) return Moves.size();
	uint64_t N = 0;
	for (const ChessMove& M : Moves) N += Apply(M).Perft(Depth - 1);
	return N;
}

// ---------------------------------------------------------------------------
// Game

Game::Game() : Game(Position::Start()) {}

Game::Game(const Position& Start) { History.push_back(Start); }

bool Game::Play(const ChessMove& M)
{
	const Position& Cur = Current();
	if (Status() != GameStatus::Ongoing || !Cur.IsLegal(M)) return false;
	SanList.push_back(Cur.ToSan(M));
	MoveList.push_back(M);
	History.push_back(Cur.Apply(M));
	return true;
}

bool Game::PlayUci(std::string_view Uci)
{
	const std::optional<ChessMove> M = ChessMove::FromUciString(Uci);
	return M && Play(*M);
}

bool Game::Undo()
{
	if (MoveList.empty()) return false;
	History.pop_back();
	MoveList.pop_back();
	SanList.pop_back();
	return true;
}

GameStatus Game::Status() const
{
	const Position& Cur = Current();
	if (Cur.LegalMoves().empty()) return Cur.InCheck() ? GameStatus::Checkmate : GameStatus::Stalemate;
	if (Cur.HasInsufficientMaterial()) return GameStatus::DrawInsufficientMaterial;
	if (Cur.HalfmoveClock() >= 100) return GameStatus::DrawFiftyMove;
	const std::string Key = Cur.RepetitionKey();
	int Count = 0;
	for (const Position& P : History)
		if (P.RepetitionKey() == Key) ++Count;
	if (Count >= 3) return GameStatus::DrawThreefold;
	return GameStatus::Ongoing;
}

std::optional<Color> Game::Winner() const
{
	if (Status() != GameStatus::Checkmate) return std::nullopt;
	return Opposite(Current().SideToMove());
}

std::string Game::UciMoveList() const
{
	std::string S;
	for (const ChessMove& M : MoveList)
	{
		if (!S.empty()) S.push_back(' ');
		S += M.ToUci();
	}
	return S;
}

// ---------------------------------------------------------------------------
// Fallback AI

namespace
{
int PieceValue(PieceType T)
{
	switch (T)
	{
	case PieceType::Pawn: return 100;
	case PieceType::Knight: return 320;
	case PieceType::Bishop: return 330;
	case PieceType::Rook: return 500;
	case PieceType::Queen: return 900;
	default: return 0;
	}
}

int CenterBonus(Square S)
{
	const int F = FileOf(S), R = RankOf(S);
	const int Df = F < 4 ? 3 - F : F - 4;
	const int Dr = R < 4 ? 3 - R : R - 4;
	return 6 - 2 * (Df + Dr);
}

int Negamax(const Position& P, int Depth, int Alpha, int Beta)
{
	const std::vector<ChessMove> Moves = P.LegalMoves();
	if (Moves.empty()) return P.InCheck() ? -100000 - Depth : 0;
	if (Depth == 0)
	{
		const int E = Evaluate(P);
		return P.SideToMove() == Color::White ? E : -E;
	}
	int Best = -1000000;
	for (const ChessMove& M : Moves)
	{
		const int Score = -Negamax(P.Apply(M), Depth - 1, -Beta, -Alpha);
		Best = std::max(Best, Score);
		Alpha = std::max(Alpha, Score);
		if (Alpha >= Beta) break;
	}
	return Best;
}
} // namespace

int Evaluate(const Position& P)
{
	int Score = 0;
	for (Square S = 0; S < 64; ++S)
	{
		const Piece& Pc = P.At(S);
		if (Pc.IsEmpty()) continue;
		int V = PieceValue(Pc.Type);
		if (Pc.Type == PieceType::Knight || Pc.Type == PieceType::Bishop || Pc.Type == PieceType::Pawn) V += CenterBonus(S) * 3;
		if (Pc.Type == PieceType::Pawn) V += (Pc.Side == Color::White ? RankOf(S) - 1 : 6 - RankOf(S)) * 5;
		Score += Pc.Side == Color::White ? V : -V;
	}
	return Score;
}

ChessMove PickFallbackMove(const Position& P, int Depth, uint32_t Seed)
{
	const std::vector<ChessMove> Moves = P.LegalMoves();
	if (Moves.empty()) return ChessMove{};
	Depth = std::max(0, Depth);
	uint32_t Rng = Seed * 2654435761u + 1u;
	ChessMove Best = Moves.front();
	int BestScore = -10000000;
	for (const ChessMove& M : Moves)
	{
		Rng ^= Rng << 13; Rng ^= Rng >> 17; Rng ^= Rng << 5;
		const int Jitter = static_cast<int>(Rng % 15);
		const int Score = -Negamax(P.Apply(M), Depth, -1000000, 1000000) * 16 + Jitter;
		if (Score > BestScore)
		{
			BestScore = Score;
			Best = M;
		}
	}
	return Best;
}

} // namespace chesscore
