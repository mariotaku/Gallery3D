#include "graphics/DrawableLoad.h"

#include "app/App.h"

namespace DrawableLoad {

void init() {}

Result load(const std::string &name, bool scaled) {
    const App::Drawable drawable = App::findDrawable(name, scaled);
    Result result;
    result.bitmap = Bitmap::load(drawable.path, 0);
    result.density = drawable.density;
    return result;
}

NinePatchSource loadNinePatch(const std::string &name) {
    const App::Drawable drawable = App::findDrawable(name);
    NinePatchSource source;
    source.bitmap = Bitmap::load(drawable.path, 0);
    source.density = drawable.density;
    return source;
}

}  // namespace DrawableLoad
