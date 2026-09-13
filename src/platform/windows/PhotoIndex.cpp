#include "media/PhotoIndex.h"

#include <algorithm>
#include <cstddef>

#include <windows.h>
// Makes oledb.h define the GUIDs it declares, which it does in one file only.
#define DBINITCONSTANTS
#include <msdasc.h>
#include <oledb.h>
#include <oledberr.h>
#include <searchapi.h>

namespace PhotoIndex {

namespace {

std::wstring wideOf(const std::string &utf8) {
    const int length = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), (int)utf8.size(), nullptr, 0);
    std::wstring wide((size_t)std::max(length, 0), L'\0');
    if (length > 0) {
        MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), (int)utf8.size(), wide.data(), length);
    }
    return wide;
}

std::string utf8Of(const wchar_t *wide, int wideLength) {
    const int length = WideCharToMultiByte(CP_UTF8, 0, wide, wideLength, nullptr, 0, nullptr, nullptr);
    std::string text((size_t)std::max(length, 0), '\0');
    if (length > 0) {
        WideCharToMultiByte(CP_UTF8, 0, wide, wideLength, text.data(), length, nullptr, nullptr);
    }
    return text;
}

// A COM pointer released when it goes out of scope.
template <typename T>
struct Com {
    T *pointer = nullptr;
    Com() = default;
    Com(const Com &) = delete;
    Com &operator=(const Com &) = delete;
    ~Com() {
        if (pointer != nullptr) {
            pointer->Release();
        }
    }
    T *operator->() const {
        return pointer;
    }
    explicit operator bool() const {
        return pointer != nullptr;
    }
};

// The columns asked for, in the order the row buffer holds them.
enum Column {
    COLUMN_PATH,
    COLUMN_DATE_TAKEN,
    COLUMN_WIDTH,
    COLUMN_HEIGHT,
    COLUMN_ORIENTATION,
    COLUMN_LATITUDE,
    COLUMN_LONGITUDE,
    COLUMN_ATTRIBUTES,
    COLUMN_COUNT,
};

const char *const kSelect = "SELECT System.ItemPathDisplay, System.Photo.DateTaken, System.Image.HorizontalSize, "
                            "System.Image.VerticalSize, System.Photo.Orientation, System.GPS.LatitudeDecimal, "
                            "System.GPS.LongitudeDecimal, System.FileAttributes FROM SystemIndex "
                            "WHERE System.Kind = 'picture' AND ";

// One cell as the rowset fills it: every column is taken as a VARIANT, and
// the provider converts whatever it stores into one.
struct Cell {
    VARIANT value;
    DBSTATUS status;
};

struct Row {
    Cell cells[COLUMN_COUNT];
};

// The cell converted to the type wanted, when it holds a value that converts.
bool cellAs(const Cell &cell, VARTYPE type, VARIANT &out) {
    VariantInit(&out);
    return cell.status == DBSTATUS_S_OK && SUCCEEDED(VariantChangeType(&out, &cell.value, 0, type));
}

void addRow(const Row &row, Entries &entries) {
    VARIANT value;
    if (!cellAs(row.cells[COLUMN_PATH], VT_BSTR, value)) {
        return;
    }
    Entry entry;
    entry.path = utf8Of(value.bstrVal, (int)SysStringLen(value.bstrVal));
    VariantClear(&value);

    Bitmap::ExifInfo &info = entry.info;
    if (cellAs(row.cells[COLUMN_DATE_TAKEN], VT_DATE, value)) {
        info.dateTakenMs = unixMsFromOleDate(value.date);
    }
    if (cellAs(row.cells[COLUMN_WIDTH], VT_UI4, value)) {
        info.pixelWidth = (int)value.ulVal;
    }
    if (cellAs(row.cells[COLUMN_HEIGHT], VT_UI4, value)) {
        info.pixelHeight = (int)value.ulVal;
    }
    if (cellAs(row.cells[COLUMN_ORIENTATION], VT_UI4, value)) {
        info.rotationDegrees = Bitmap::degreesForOrientation(value.ulVal);
        // The item's size is the stored pixels', which the rotation then turns,
        // as it is when read from EXIF.
        storedSize(value.ulVal, &info.pixelWidth, &info.pixelHeight);
    }
    // A picture with no position leaves both at zero, which is how ExifInfo
    // says there is none.
    if (cellAs(row.cells[COLUMN_LATITUDE], VT_R8, value)) {
        info.latitude = value.dblVal;
    }
    if (cellAs(row.cells[COLUMN_LONGITUDE], VT_R8, value)) {
        info.longitude = value.dblVal;
    }
    if (cellAs(row.cells[COLUMN_ATTRIBUTES], VT_UI4, value)) {
        entry.attributes = value.ulVal;
    }
    const std::string entryKey = key(entry.path);
    entries[entryKey] = std::move(entry);
}

// The folders the indexer crawls. A folder it does not has no pictures in the
// index however many are on the disk.
std::vector<std::string> crawledFolders(ISearchCatalogManager *catalog, const std::vector<std::string> &folders) {
    std::vector<std::string> crawled;
    Com<ISearchCrawlScopeManager> scope;
    if (FAILED(catalog->GetCrawlScopeManager(&scope.pointer))) {
        return crawled;
    }
    for (const std::string &folder : folders) {
        const std::wstring url = L"file:///" + wideOf(folder);
        BOOL included = FALSE;
        if (SUCCEEDED(scope->IncludedInCrawlScope(url.c_str(), &included)) && included) {
            crawled.push_back(folder);
        }
    }
    return crawled;
}

// Asks the index. Any step that fails leaves whatever was read so far, and
// lists no folder whole.
void run(const std::vector<std::string> &folders, Listing &listing) {
    Com<ISearchManager> manager;
    if (FAILED(CoCreateInstance(__uuidof(CSearchManager), nullptr, CLSCTX_LOCAL_SERVER,
                                IID_PPV_ARGS(&manager.pointer)))) {
        return;
    }
    Com<ISearchCatalogManager> catalog;
    if (FAILED(manager->GetCatalog(L"SystemIndex", &catalog.pointer))) {
        return;
    }
    std::vector<std::string> crawled = crawledFolders(catalog.pointer, folders);
    Com<ISearchQueryHelper> helper;
    if (FAILED(catalog->GetQueryHelper(&helper.pointer))) {
        return;
    }
    LPWSTR connection = nullptr;
    if (FAILED(helper->get_ConnectionString(&connection))) {
        return;
    }

    Com<IDataInitialize> dataInitialize;
    Com<IDBInitialize> dataSource;
    HRESULT result = CoCreateInstance(__uuidof(MSDAINITIALIZE), nullptr, CLSCTX_INPROC_SERVER,
                                      IID_PPV_ARGS(&dataInitialize.pointer));
    if (SUCCEEDED(result)) {
        result = dataInitialize->GetDataSource(nullptr, CLSCTX_INPROC_SERVER, connection,
                                               __uuidof(IDBInitialize), (IUnknown **)&dataSource.pointer);
    }
    CoTaskMemFree(connection);
    if (FAILED(result) || FAILED(dataSource->Initialize())) {
        return;
    }

    Com<IDBCreateSession> sessionFactory;
    Com<IDBCreateCommand> session;
    Com<ICommandText> command;
    if (FAILED(dataSource->QueryInterface(IID_PPV_ARGS(&sessionFactory.pointer))) ||
        FAILED(sessionFactory->CreateSession(nullptr, __uuidof(IDBCreateCommand), (IUnknown **)&session.pointer)) ||
        FAILED(session->CreateCommand(nullptr, __uuidof(ICommandText), (IUnknown **)&command.pointer))) {
        return;
    }
    const std::wstring sql = wideOf(std::string(kSelect) + scopeClause(folders));
    Com<IRowset> rowset;
    if (FAILED(command->SetCommandText(DBGUID_DEFAULT, sql.c_str())) ||
        FAILED(command->Execute(nullptr, __uuidof(IRowset), nullptr, nullptr, (IUnknown **)&rowset.pointer)) ||
        !rowset) {
        return;
    }

    DBBINDING bindings[COLUMN_COUNT] = {};
    for (int column = 0; column < COLUMN_COUNT; ++column) {
        DBBINDING &binding = bindings[column];
        // Ordinal zero is the bookmark; the selected columns start at one.
        binding.iOrdinal = (DBORDINAL)column + 1;
        binding.obValue = offsetof(Row, cells) + (DBBYTEOFFSET)column * sizeof(Cell) + offsetof(Cell, value);
        binding.obStatus = offsetof(Row, cells) + (DBBYTEOFFSET)column * sizeof(Cell) + offsetof(Cell, status);
        binding.dwPart = DBPART_VALUE | DBPART_STATUS;
        binding.dwMemOwner = DBMEMOWNER_CLIENTOWNED;
        binding.eParamIO = DBPARAMIO_NOTPARAM;
        binding.cbMaxLen = sizeof(VARIANT);
        binding.wType = DBTYPE_VARIANT;
    }
    Com<IAccessor> accessorFactory;
    HACCESSOR accessor = DB_NULL_HACCESSOR;
    if (FAILED(rowset->QueryInterface(IID_PPV_ARGS(&accessorFactory.pointer))) ||
        FAILED(accessorFactory->CreateAccessor(DBACCESSOR_ROWDATA, COLUMN_COUNT, bindings, 0, &accessor, nullptr))) {
        return;
    }

    HROW handles[256];
    bool complete = false;
    for (;;) {
        DBCOUNTITEM obtained = 0;
        HROW *fetchedHandles = handles;
        const HRESULT fetched = rowset->GetNextRows(DB_NULL_HCHAPTER, 0, 256, &obtained, &fetchedHandles);
        for (DBCOUNTITEM i = 0; i < obtained; ++i) {
            Row row = {};
            if (SUCCEEDED(rowset->GetData(fetchedHandles[i], accessor, &row))) {
                addRow(row, listing.entries);
            }
            for (Cell &cell : row.cells) {
                if (cell.status == DBSTATUS_S_OK) {
                    VariantClear(&cell.value);
                }
            }
        }
        if (obtained > 0) {
            rowset->ReleaseRows(obtained, fetchedHandles, nullptr, nullptr, nullptr);
        }
        if (fetched != S_OK || obtained == 0) {
            // Only the end of the rows says every picture was read. A failure
            // part way through leaves the folders to be walked.
            complete = fetched == DB_S_ENDOFROWSET || (fetched == S_OK && obtained == 0);
            break;
        }
    }
    accessorFactory->ReleaseAccessor(accessor, nullptr);
    if (complete) {
        listing.listedFolders = std::move(crawled);
    }
}

}  // namespace

Listing query(const std::vector<std::string> &folders) {
    Listing listing;
    if (folders.empty()) {
        return listing;
    }
    // Scans run on the feed's loader thread. The index lives in another
    // process, and the free threaded apartment is enough to reach it.
    const HRESULT initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    run(folders, listing);
    if (SUCCEEDED(initialized)) {
        CoUninitialize();
    }
    return listing;
}

}  // namespace PhotoIndex
