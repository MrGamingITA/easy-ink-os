// La Goccia del firmware è un CAVT (gfx/avatar_builtin.cpp, da avatars/goccia.avatar.json) disegnato da
// drawAvatar(): deve coincidere pixel per pixel con mascot() del simulatore (golden.h, generato da
// tools/golden/mascot-golden.js, invariato).
#include <unity.h>
#include <stdio.h>
#include "gfx/avatar.h"
#include "gfx/avatar_builtin.h"
#include "golden.h"

void setUp() {}
void tearDown() {}

static void check_pose(int p) {
  const Avatar &g0 = gocciaAvatar();
  TEST_ASSERT_EQUAL_UINT8(4, g0.frames);  // CAVT valido: 9 pose × 4 fotogrammi
  for (int f = 0; f < 4; f++) {
    Canvas<GOLDEN_W, GOLDEN_H> cv;
    cv.clear();
    CanvasSurface<GOLDEN_W, GOLDEN_H> s(cv);
    drawAvatar(s, g0, GOLDEN_OX, GOLDEN_OY, 1, (Pose)p, f);
    const char *g = GOLDEN[p][f];
    for (int y = 0; y < GOLDEN_H; y++)
      for (int x = 0; x < GOLDEN_W; x++) {
        bool want = g[y * GOLDEN_W + x] == '#';
        if (cv.get(x, y) != want) {
          char msg[96];
          snprintf(msg, sizeof msg, "posa %s fotogramma %d: pixel (%d,%d) diverso", poseLabel((Pose)p), f, x, y);
          TEST_FAIL_MESSAGE(msg);
        }
      }
  }
}

static void test_working() { check_pose(0); }
static void test_permission() { check_pose(1); }
static void test_input() { check_pose(2); }
static void test_done() { check_pose(3); }
static void test_error() { check_pose(4); }
static void test_idle() { check_pose(5); }
static void test_compact() { check_pose(6); }
static void test_tired() { check_pose(7); }
static void test_stuffy() { check_pose(8); }

static void test_scale_and_invert() {
  Canvas<90, 90> cv;
  cv.clear();
  CanvasSurface<90, 90> s(cv);
  drawAvatar(s, gocciaAvatar(), 0, 9, 3, Pose::Working, 0);
  int n3 = cv.countInk();
  Canvas<30, 30> c1;
  c1.clear();
  CanvasSurface<30, 30> s1(c1);
  drawAvatar(s1, gocciaAvatar(), 0, 3, 1, Pose::Working, 0);
  TEST_ASSERT_EQUAL(c1.countInk() * 9, n3);  // scala 3 = ogni punto diventa 3×3
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_working);
  RUN_TEST(test_permission);
  RUN_TEST(test_input);
  RUN_TEST(test_done);
  RUN_TEST(test_error);
  RUN_TEST(test_idle);
  RUN_TEST(test_compact);
  RUN_TEST(test_tired);
  RUN_TEST(test_stuffy);
  RUN_TEST(test_scale_and_invert);
  return UNITY_END();
}
