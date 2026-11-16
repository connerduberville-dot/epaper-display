// Train board (firmware/Display/train_board.h + train_api.h) on the PC: parses a saved SEPTA NextToArrive
// response, renders the board, and checks minute ticks against the ghost rule. Needs single-header ArduinoJson 7.3.1:
//   python -m ziglang c++ -std=c++17 -I<dir with ArduinoJson.h> train_board_test.cpp -o tbt.exe && tbt.exe
#include <ArduinoJson.h>
#include <cstdio>
#include <fstream>
#include <sstream>
#include "../firmware/Display/train_api.h"
using namespace septa;

static void savePBM(const Framebuffer& fb, const char* path) {
  FILE* f = fopen(path, "wb"); fprintf(f, "P4\n%d %d\n", W, H); fwrite(fb.px, 1, FB_BYTES, f); fclose(f);
}

int main() {
  std::ifstream in("data/nexttoarrive_ftwashington.json"); std::stringstream ss; ss << in.rdbuf();
  static Train t[8];
  int n = parseDepartures(ss.str().c_str(), t, 8);
  printf("parsed %d trains; first %s at %s (%d min), %s\n", n, t[0].number, t[0].departure, t[0].schedMin, t[0].delayRaw);
  int fails = 0;
  if (n < 3 || t[0].schedMin < 0) { printf("FAIL: parse\n"); ++fails; }
  if (parseDepartures("{\"error\":\"x\"}", t + 7, 1) != -1) { printf("FAIL: non-array accepted\n"); ++fails; }

  static Framebuffer a, b;
  BoardData d; d.footerRight = "BUILD 10"; snprintf(d.updated, sizeof d.updated, "3:21 PM");
  d.nowMin = t[0].schedMin - 12; snprintf(d.clock, sizeof d.clock, "3:21 PM");
  d.n = pickUpcoming(t, n, d.nowMin, d.trains, 3);
  renderV2(a, d); savePBM(a, "train_board.pbm");
  // A minute tick (clock + countdown change) must stay a partial refresh.
  d.nowMin += 1; snprintf(d.clock, sizeof d.clock, "3:22 PM"); renderV2(b, d);
  printf("minute tick clears %u px (limit 1500)\n", b.clearedPixels(a));
  // A delay appearing on the next train.
  snprintf(d.trains[0].delayRaw, sizeof d.trains[0].delayRaw, "5 min"); d.trains[0].lateMin = 5; renderV2(a, d); savePBM(a, "train_board_late.pbm");
  BoardData f; f.fault = "FEED UNAVAILABLE"; f.faultDetail = "FEED ERROR -1"; f.footerRight = "BUILD 10"; renderV2(b, f); savePBM(b, "train_board_fault.pbm");
  printf(fails ? "%d FAILED\n" : "train board checks passed\n", fails);
  return fails;
}
