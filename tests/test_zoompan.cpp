// Panning a zoomed-in picture stops at the safe area's edges, so the system
// bars and the cutout never hide its last row or column.
#include "tests.h"

#include "app/App.h"
#include "grid/GridCamera.h"
#include "grid/GridCameraManager.h"
#include "core/Vector3f.h"

namespace {

struct ScopedSafeArea {
    ScopedSafeArea(float left, float top, float right, float bottom) : previous(App::SAFE_AREA) {
        App::SAFE_AREA.left = left;
        App::SAFE_AREA.top = top;
        App::SAFE_AREA.right = right;
        App::SAFE_AREA.bottom = bottom;
    }
    ~ScopedSafeArea() {
        App::SAFE_AREA = previous;
    }
    App::SafeAreaInsets previous;
};

// One slot, at the origin.
class OriginLayout : public LayoutInterface {
  public:
    void getPositionForSlotIndex(int slotIndex, int itemWidth, int itemHeight, Vector3f &outPosition) override {
        (void)slotIndex;
        (void)itemWidth;
        (void)itemHeight;
        outPosition.set(0.0f, 0.0f, 0.0f);
    }
};

const int kWindowWidth = 1080;
const int kWindowHeight = 2400;

struct Edges {
    float left;
    float top;
    float right;
    float bottom;
};

// The safe area in world units, where the camera is heading.
Edges safeEdges(GridCamera &camera) {
    Vector3f topLeft;
    Vector3f bottomRight;
    const App::SafeAreaInsets &safe = App::SAFE_AREA;
    camera.convertToCameraSpace(safe.left, safe.top, 0.0f, topLeft);
    camera.convertToCameraSpace((float)kWindowWidth - safe.right, (float)kWindowHeight - safe.bottom, 0.0f,
                                bottomRight);
    return Edges{topLeft.x, topLeft.y, bottomRight.x, bottomRight.y};
}

// Pans in steps, the way a drag arrives, then lets go.
void panAndRelease(GridCamera &camera, float stepX, float stepY, float width, float height) {
    for (int i = 0; i < 20; ++i) {
        camera.moveBy(stepX, stepY, 0.0f);
    }
    OriginLayout layout;
    GridCameraManager manager(&camera);
    manager.constrainCameraForSlot(&layout, 0, Vector3f(0.0f, 0.0f, 0.0f), width, height);
}

}  // namespace

TEST(zoomed_picture_stops_at_the_safe_area_top_left) {
    ScopedSafeArea insets(40.0f, 132.0f, 0.0f, 63.0f);
    GridCamera camera(kWindowWidth, kWindowHeight, 256, 192);
    // Wider and taller than the window, as a picture zoomed in is. A world
    // unit here is one item height, so the window is 5.6 units wide and 12.5
    // tall. The camera caps a sideways move, so the pan stays within one
    // correction of the edge.
    const float width = 12.0f;
    const float height = 60.0f;

    panAndRelease(camera, -0.25f, -3.0f, width, height);

    const Edges safe = safeEdges(camera);
    CHECK_NEAR(safe.left, -width * 0.5f, 1e-4f);
    CHECK_NEAR(safe.top, -height * 0.5f, 1e-4f);
}

TEST(zoomed_picture_stops_at_the_safe_area_bottom_right) {
    ScopedSafeArea insets(40.0f, 132.0f, 24.0f, 63.0f);
    GridCamera camera(kWindowWidth, kWindowHeight, 256, 192);
    const float width = 12.0f;
    const float height = 60.0f;

    panAndRelease(camera, 0.25f, 3.0f, width, height);

    const Edges safe = safeEdges(camera);
    CHECK_NEAR(safe.right, width * 0.5f, 1e-4f);
    CHECK_NEAR(safe.bottom, height * 0.5f, 1e-4f);
}

TEST(picture_narrower_than_the_safe_area_centres_in_it) {
    // More taken from the top than the bottom, so the middle of the safe area
    // is not the middle of the window.
    ScopedSafeArea insets(0.0f, 132.0f, 0.0f, 63.0f);
    GridCamera camera(kWindowWidth, kWindowHeight, 256, 192);
    const float width = 2.0f;
    const float height = 3.0f;

    panAndRelease(camera, 0.25f, 3.0f, width, height);

    const Edges safe = safeEdges(camera);
    CHECK_NEAR((safe.left + safe.right) * 0.5f, 0.0f, 1e-4f);
    CHECK_NEAR((safe.top + safe.bottom) * 0.5f, 0.0f, 1e-4f);
}
