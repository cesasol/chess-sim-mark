// SPDX-License-Identifier: Apache-2.0
// Standalone tests for the engine-independent chess core. Run: just test-core
#include "Chess/ChessCore.h"

#include <cstdio>
#include <string>

using namespace chesscore;

static int Failures = 0;
static int Passed = 0;

#define EXPECT(cond)                                                                 \
	do {                                                                             \
		if (cond) ++Passed;                                                          \
		else { ++Failures; std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } \
	} while (0)

static Position Fen(const char* F)
{
	auto P = Position::FromFen(F);
	if (!P) { std::printf("bad fen %s\n", F); ++Failures; return Position::Start(); }
	return *P;
}

static void TestPerft()
{
	// Reference counts from https://www.chessprogramming.org/Perft_Results
	struct Case { const char* Fen; int Depth; uint64_t Nodes; };
	const Case Cases[] = {
		{Position::StartFen, 1, 20}, {Position::StartFen, 2, 400}, {Position::StartFen, 3, 8902}, {Position::StartFen, 4, 197281},
		{"r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1", 1, 48},
		{"r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1", 2, 2039},
		{"r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1", 3, 97862},
		{"8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1", 4, 43238},
		{"8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1", 5, 674624},
		{"r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1", 3, 9467},
		{"rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8", 3, 62379},
		{"r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10", 3, 89890},
	};
	for (const Case& C : Cases)
	{
		const uint64_t N = Fen(C.Fen).Perft(C.Depth);
		if (N != C.Nodes) std::printf("perft %s d%d = %llu, want %llu\n", C.Fen, C.Depth, (unsigned long long)N, (unsigned long long)C.Nodes);
		EXPECT(N == C.Nodes);
	}
}

static void TestFenRoundTrip()
{
	const char* Fens[] = {
		Position::StartFen,
		"r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
		"rnbqkbnr/pp1ppppp/8/2p5/4P3/8/PPPP1PPP/RNBQKBNR w KQkq c6 0 2",
	};
	for (const char* F : Fens) EXPECT(Fen(F).ToFen() == F);
	EXPECT(!Position::FromFen("garbage"));
	EXPECT(!Position::FromFen("8/8/8/8/8/8/8/8 w - - 0 1")); // no kings
}

static void TestSan()
{
	Game G;
	EXPECT(G.PlayUci("e2e4"));
	EXPECT(G.PlayUci("e7e5"));
	EXPECT(G.PlayUci("g1f3"));
	EXPECT(G.SanMoves().back() == "Nf3");
	EXPECT(!G.PlayUci("e1e3")); // illegal
	Position P = Fen("4k3/8/8/8/8/8/8/R3K2R w KQ - 0 1");
	auto M = P.ParseSan("O-O");
	EXPECT(M && M->ToUci() == "e1g1");
	EXPECT(P.ToSan(*ChessMove::FromUciString("a1a8")) == "Ra8+");
	// Disambiguation: two knights can reach d2.
	Position Q = Fen("4k3/8/8/8/8/8/8/1N2KN2 w - - 0 1");
	EXPECT(Q.ToSan(*ChessMove::FromUciString("b1d2")) == "Nbd2");
	// Promotion with mate.
	Position R = Fen("k7/4P3/1K6/8/8/8/8/8 w - - 0 1");
	EXPECT(R.ToSan(*ChessMove::FromUciString("e7e8q")) == "e8=Q#");
}

static void TestGameEnd()
{
	Game Fool;
	for (const char* M : {"f2f3", "e7e5", "g2g4", "d8h4"}) EXPECT(Fool.PlayUci(M));
	EXPECT(Fool.Status() == GameStatus::Checkmate);
	EXPECT(Fool.Winner() && *Fool.Winner() == Color::Black);
	EXPECT(!Fool.PlayUci("a2a3")); // game over

	Game Stale(Fen("7k/5Q2/6K1/8/8/8/8/8 b - - 0 1"));
	EXPECT(Stale.Status() == GameStatus::Stalemate);

	Game Bare(Fen("8/8/4k3/8/8/3K4/8/8 w - - 0 1"));
	EXPECT(Bare.Status() == GameStatus::DrawInsufficientMaterial);
	Game KNvK(Fen("8/8/4k3/8/8/3K1N2/8/8 w - - 0 1"));
	EXPECT(KNvK.Status() == GameStatus::DrawInsufficientMaterial);
	Game KRvK(Fen("8/8/4k3/8/8/3K1R2/8/8 w - - 0 1"));
	EXPECT(KRvK.Status() == GameStatus::Ongoing);

	Game Rep;
	for (int I = 0; I < 2; ++I)
		for (const char* M : {"g1f3", "g8f6", "f3g1", "f6g8"}) EXPECT(Rep.PlayUci(M));
	EXPECT(Rep.Status() == GameStatus::DrawThreefold);

	Game Fifty(Fen("8/8/4k3/8/8/3K1R2/8/8 w - - 99 80"));
	EXPECT(Fifty.PlayUci("f3f4"));
	EXPECT(Fifty.Status() == GameStatus::DrawFiftyMove);
}

static void TestEnPassantAndUndo()
{
	Game G(Fen("4k3/8/8/3pP3/8/8/8/4K3 w - d6 0 1"));
	EXPECT(G.Current().IsCapture(*ChessMove::FromUciString("e5d6")));
	EXPECT(G.PlayUci("e5d6"));
	EXPECT(G.SanMoves().back() == "exd6");
	EXPECT(G.Current().At(ParseSquare("d5")).IsEmpty());
	EXPECT(G.Undo());
	EXPECT(G.Current().At(ParseSquare("d5")).Is(Color::Black, PieceType::Pawn));
	EXPECT(G.UciMoveList().empty());
}

static void TestFallbackAi()
{
	// Mate in one must be found: Qh5xf7#... use a simple back-rank mate.
	Position P = Fen("6k1/5ppp/8/8/8/8/8/R5K1 w - - 0 1");
	ChessMove M = PickFallbackMove(P, 1, 7);
	EXPECT(M.ToUci() == "a1a8");
	// Free queen capture is taken.
	Position Q = Fen("4k3/8/8/3q4/4P3/8/8/4K3 w - - 0 1");
	EXPECT(PickFallbackMove(Q, 1, 3).ToUci() == "e4d5");
	EXPECT(!PickFallbackMove(Fen("7k/5Q2/6K1/8/8/8/8/8 b - - 0 1"), 2, 1).IsValid());
}

int main()
{
	TestFallbackAi();
	TestFenRoundTrip();
	TestSan();
	TestGameEnd();
	TestEnPassantAndUndo();
	TestPerft();
	std::printf("%d passed, %d failed\n", Passed, Failures);
	return Failures == 0 ? 0 : 1;
}
