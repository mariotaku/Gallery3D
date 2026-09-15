# Small shared pieces: caches, dates, maths and the crash handler's interface.
target_sources(gallery3d_core PRIVATE
    Backtrace.h
    ByteCache.cpp
    ByteCache.h
    DateLabels.cpp
    DateLabels.h
    Dates.h
    DiskCache.cpp
    DiskCache.h
    FloatAnim.cpp
    FloatAnim.h
    FloatUtils.h
    IndexRange.h
    JsonValue.h
    LocalTime.h
    MatrixStack.cpp
    MatrixStack.h
    Md5.cpp
    Md5.h
    Shared.h
    StableId.h
    Vector3f.h)
