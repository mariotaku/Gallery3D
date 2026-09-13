// Choosing the photo the blurred background is made from.
//
// The wall takes the middle of what is on screen, which is right when several
// stacks are visible at once. Fullscreen shows one photo and several slots, so
// the middle of the range lands on a neighbour and the background ends up
// blurring a photo that is not the one being looked at.
#include "tests.h"

#include "grid/GridLayer.h"
#include "core/Shared.h"

namespace {

const int kNoSlot = Shared::INVALID;

int representativeFor(int state, int focusSlot, int selectedSlot, int anchorCenterSlot) {
    return GridLayer::representativeSlotIndex(state, focusSlot, selectedSlot, anchorCenterSlot);
}

}  // namespace

TEST(the_wall_blurs_the_middle_of_what_is_visible) {
    // Several stacks on screen and none of them singled out, so the one in the
    // middle stands for the view.
    CHECK_EQ(representativeFor(GridLayer::STATE_MEDIA_SETS, kNoSlot, kNoSlot, 4), 4);
    CHECK_EQ(representativeFor(GridLayer::STATE_GRID_VIEW, kNoSlot, kNoSlot, 7), 7);
    CHECK_EQ(representativeFor(GridLayer::STATE_TIMELINE, kNoSlot, kNoSlot, 2), 2);
}

TEST(fullscreen_blurs_the_photo_on_screen) {
    // The case that was wrong: showing slot 3 over a visible range of [1,3],
    // whose middle is 2. The background belongs to 3.
    CHECK_EQ(representativeFor(GridLayer::STATE_FULL_SCREEN, kNoSlot, 3, 2), 3);

    // And it keeps up as the photo steps along, rather than settling on
    // whatever the range happens to straddle.
    for (int photo = 0; photo < 6; ++photo) {
        CHECK_EQ(representativeFor(GridLayer::STATE_FULL_SCREEN, kNoSlot, photo, 2), photo);
    }
}

TEST(a_focused_slot_is_taken_whatever_the_view) {
    // Pressing a slot puts the background on it, in every state.
    const int states[] = {GridLayer::STATE_MEDIA_SETS, GridLayer::STATE_GRID_VIEW, GridLayer::STATE_FULL_SCREEN,
                          GridLayer::STATE_TIMELINE};
    for (int state : states) {
        CHECK_EQ(representativeFor(state, 5, 3, 2), 5);
    }
}

TEST(fullscreen_with_nothing_selected_still_has_a_background) {
    // No photo picked out yet, so there is nothing better than the middle.
    CHECK_EQ(representativeFor(GridLayer::STATE_FULL_SCREEN, kNoSlot, kNoSlot, 2), 2);
}

TEST(the_wall_ignores_a_selection_left_over_from_fullscreen) {
    // Coming back to the wall, the photo that was open is no longer what the
    // view is about. Only fullscreen reads the selection.
    CHECK_EQ(representativeFor(GridLayer::STATE_GRID_VIEW, kNoSlot, 9, 4), 4);
    CHECK_EQ(representativeFor(GridLayer::STATE_MEDIA_SETS, kNoSlot, 9, 4), 4);
}
