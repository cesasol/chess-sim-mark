// SPDX-License-Identifier: Apache-2.0
//
// Engine-independent chess rules: positions, FEN, legal move generation,
// UCI/SAN notation and game-end detection. Deliberately free of Unreal
// headers so it can be unit tested with a plain compiler (`just test-core`).
// Avoid identifiers that collide with Unreal macros (check, verify, ensure).
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace chesscore
{

enum class Color : uint8_t { White = 0, Black = 1 };

inline Color Opposite(Color C) { return C == Color::White ? Color::Black : Color::White; }

enum class PieceType : uint8_t { None = 0, Pawn, Knight, Bishop, Rook, Queen, King };

struct Piece
{
	PieceType Type = PieceType::None;
	Color Side = Color::White;

	bool IsEmpty() const { return Type == PieceType::None; }
	bool Is(Color C, PieceType T) const { return Type == T && Side == C; }
	bool operator==(const Piece& O) const { return Type == O.Type && (Type == PieceType::None || Side == O.Side); }
	char ToFenChar() const;
	static std::optional<Piece> FromFenChar(char C);
};

// Square index 0..63, a1 = 0, h1 = 7, a8 = 56, h8 = 63.
using Square = int8_t;
constexpr Square NoSquare = -1;

inline int FileOf(Square S) { return S & 7; }
inline int RankOf(Square S) { return S >> 3; }
inline Square MakeSquare(int File, int Rank) { return static_cast<Square>(Rank * 8 + File); }
std::string SquareName(Square S);
Square ParseSquare(std::string_view Name);

enum CastlingRights : uint8_t
{
	CastleNone = 0,
	CastleWK = 1,
	CastleWQ = 2,
	CastleBK = 4,
	CastleBQ = 8,
	CastleAll = 15,
};

struct ChessMove
{
	Square From = NoSquare;
	Square To = NoSquare;
	PieceType Promotion = PieceType::None;

	bool IsValid() const { return From != NoSquare && To != NoSquare; }
	bool operator==(const ChessMove& O) const { return From == O.From && To == O.To && Promotion == O.Promotion; }
	std::string ToUci() const;
	static std::optional<ChessMove> FromUciString(std::string_view Uci);
};

enum class GameStatus : uint8_t
{
	Ongoing,
	Checkmate,
	Stalemate,
	DrawFiftyMove,
	DrawThreefold,
	DrawInsufficientMaterial,
};

bool IsDraw(GameStatus S);

class Position
{
public:
	static constexpr const char* StartFen = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";

	Position();
	static std::optional<Position> FromFen(std::string_view Fen);
	static Position Start();

	std::string ToFen() const;
	// FEN without the move counters; used as the repetition key.
	std::string RepetitionKey() const;

	const Piece& At(Square S) const { return Board[S]; }
	Color SideToMove() const { return Side; }
	uint8_t Castling() const { return CastleRights; }
	Square EnPassant() const { return EpSquare; }
	int HalfmoveClock() const { return Halfmove; }
	int FullmoveNumber() const { return Fullmove; }

	std::vector<ChessMove> LegalMoves() const;
	std::vector<ChessMove> LegalMovesFrom(Square From) const;
	bool IsLegal(const ChessMove& M) const;
	// Applies a move assumed to be legal and returns the resulting position.
	Position Apply(const ChessMove& M) const;

	bool InCheck() const { return IsKingAttacked(Side); }
	bool IsSquareAttacked(Square S, Color By) const;
	bool HasInsufficientMaterial() const;
	bool IsCapture(const ChessMove& M) const;
	Square KingSquare(Color C) const;

	// Standard Algebraic Notation for a legal move in this position (includes +/#).
	std::string ToSan(const ChessMove& M) const;
	std::optional<ChessMove> ParseSan(std::string_view San) const;

	uint64_t Perft(int Depth) const;

private:
	void GeneratePseudoLegal(std::vector<ChessMove>& Out) const;
	bool IsKingAttacked(Color C) const;

	std::array<Piece, 64> Board{};
	Color Side = Color::White;
	uint8_t CastleRights = CastleNone;
	Square EpSquare = NoSquare;
	int Halfmove = 0;
	int Fullmove = 1;
};

// A game: position history for repetition detection plus the move list.
class Game
{
public:
	Game();
	explicit Game(const Position& Start);

	const Position& Current() const { return History.back(); }
	const std::vector<ChessMove>& Moves() const { return MoveList; }
	const std::vector<std::string>& SanMoves() const { return SanList; }

	// Returns false (and leaves state unchanged) if the move is illegal.
	bool Play(const ChessMove& M);
	bool PlayUci(std::string_view Uci);
	bool Undo();

	GameStatus Status() const;
	// Winner for decisive results; empty optional for draws / ongoing.
	std::optional<Color> Winner() const;
	// Move list in UCI, space separated, suitable for "position startpos moves ...".
	std::string UciMoveList() const;
	std::string StartFen() const { return History.front().ToFen(); }

private:
	std::vector<Position> History;
	std::vector<ChessMove> MoveList;
	std::vector<std::string> SanList;
};

// Static evaluation in centipawns from White's point of view (material + piece-square bonus).
int Evaluate(const Position& P);

// Small alpha-beta search used when no UCI engine is available. Seed adds
// tie-breaking randomness so games vary. Returns an invalid move if none.
ChessMove PickFallbackMove(const Position& P, int Depth, uint32_t Seed);

} // namespace chesscore
