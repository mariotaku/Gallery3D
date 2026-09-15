# Where photos come from and how they are grouped: the sources, the feed the
# wall reads, and what can be done to a photo's file.
target_sources(gallery3d_core PRIVATE
    ConcatenatedDataSource.cpp
    ConcatenatedDataSource.h
    FileOperations.cpp
    FileOperations.h
    LocalDataSource.cpp
    LocalDataSource.h
    MediaBucketList.cpp
    MediaBucketList.h
    MediaClustering.cpp
    MediaClustering.h
    MediaDetails.cpp
    MediaDetails.h
    MediaFeed.cpp
    MediaFeed.h
    MediaItem.h
    MediaSet.cpp
    MediaSet.h
    MediaStoreClient.h
    MediaStoreDataSource.cpp
    MediaStoreDataSource.h
    PhotoIndex.cpp
    PhotoIndex.h
    PhotoLibrary.cpp
    PhotoLibrary.h)
