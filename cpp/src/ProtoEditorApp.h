#ifndef PROTOEDITORAPP_H
#define PROTOEDITORAPP_H

#include <Windows.h>
#include <set>
#include <string>
#include <vector>

#include "AppPreferences.h"
#include "ProtoConfig.h"
#include "TsvFile.h"

struct ID3D11Device;
struct ID3D11DeviceContext;
struct IDXGISwapChain;
struct ID3D11RenderTargetView;

class ProtoEditorApp {
public:
    explicit ProtoEditorApp(HINSTANCE instance);
    ~ProtoEditorApp();

    int run();

private:
    enum class DatasetKind {
        Item = 0,
        Mob = 1,
    };

    struct DatasetState {
        struct CellChange {
            size_t row = 0;
            int column = -1;
            std::wstring before;
            std::wstring after;
        };

        struct ValidationIssue {
            std::string severity;
            std::wstring message;
            int row = -1;
            int column = -1;
        };

        struct SnapshotInfo {
            std::wstring path;
            std::wstring timestamp;
        };

        struct DiffEntry {
            std::wstring key;
            std::wstring type;
            int changedCells = 0;
        };

        struct RulePreset {
            std::string id;
            std::wstring name;
            std::wstring description;
        };

        DatasetKind kind = DatasetKind::Item;
        std::wstring title;
        std::wstring filePath;
        std::wstring namesPath;
        std::wstring configPath;
        TsvFile table;
        TsvFile namesTable;
        TsvFile compareTable;
        ProtoConfig config;
        std::vector<size_t> filteredRows;
        std::set<std::pair<size_t, int>> modifiedCells;
        bool loaded = false;
        bool modified = false;
        bool namesLoaded = false;
        bool compareLoaded = false;
        int selectedRow = -1;
        int selectedColumn = -1;
        int filterColumn = -1;
        int sortColumn = 0;
        bool sortAscending = true;
        std::string filterText;
        std::vector<CellChange> undoStack;
        std::vector<CellChange> redoStack;
        std::string gotoRowBuffer;
        std::vector<ValidationIssue> validationIssues;
        std::vector<SnapshotInfo> snapshots;
        std::vector<DiffEntry> compareEntries;
        std::vector<RulePreset> rulePresets;
        std::set<int> hiddenColumns;
        std::set<int> pinnedColumns;
        std::string searchEverywhereBuffer;
        std::vector<size_t> searchMatches;
        int searchMatchIndex = -1;
        std::string advancedFilterColumn;
        std::string advancedFilterOperator;
        std::string advancedFilterValue;
        bool compareOnlyChanged = true;
        bool linkedEditing = false;
        bool changedRowsOnlyExport = false;
        std::string workspacePreset = "default";
    };

    bool initializeWindow();
    bool initializeD3D();
    void cleanupD3D();
    void createRenderTarget();
    void cleanupRenderTarget();
    void initializeImGui();
    void shutdownImGui();

    void renderFrame();
    void applyTheme();

    void drawMenuBar();
    void drawToolbar();
    void drawSidebar();
    void drawTablePanel();
    void drawInspectorPanel();
    void drawStatusBar();
    void drawEditCellModal();
    void drawFlagEditorModal();
    void drawAboutModal();
    void drawGotoRowModal();
    void drawColumnManagerModal();
    void drawBulkEditModal();
    void drawColumnOrderModal();
    void drawValidationPanel();
    void drawHistoryPanel();
    void drawSearchPanel();
    void drawComparePanel();
    void drawLinkedNamesPanel();
    void drawWorkspacePanel();
    void drawExportModal();
    void drawRulePresetPanel();
    void drawThemeBuilder();

    void rebuildFilteredRows(DatasetState& dataset);
    void sortFilteredRows(DatasetState& dataset);
    void loadDataset(DatasetKind kind, const std::wstring& explicitPath = L"");
    bool saveDataset(DatasetState& dataset, bool saveAs);
    void refreshValidation(DatasetState& dataset);
    void refreshSnapshots(DatasetState& dataset);
    bool createSnapshot(DatasetState& dataset);
    bool exportCsv(const DatasetState& dataset, const std::wstring& path, bool changedRowsOnly) const;
    bool exportDiff(const DatasetState& dataset, const std::wstring& path) const;
    bool exportSql(const DatasetState& dataset, const std::wstring& path) const;
    void refreshCompare(DatasetState& dataset);
    void refreshSearchMatches(DatasetState& dataset);
    void applyAdvancedFilter(DatasetState& dataset);
    void loadLinkedNames(DatasetState& dataset);
    void syncLinkedNamesFromProto(DatasetState& dataset);
    void applyWorkspacePreset(DatasetState& dataset, const std::string& presetId);
    void executeRulePreset(DatasetState& dataset, const std::string& presetId);
    void setCellValue(DatasetState& dataset, size_t sourceRow, int column, const std::wstring& value);
    void applyCellChange(DatasetState& dataset, const DatasetState::CellChange& change, bool useAfterValue, bool trackReverse);
    void openCellEditor(DatasetState& dataset, size_t sourceRow, int column);
    void openFlagEditor(DatasetState& dataset, size_t sourceRow, int column);
    void duplicateSelectedRow(DatasetState& dataset);
    void insertEmptyRow(DatasetState& dataset);
    void deleteSelectedRow(DatasetState& dataset);
    void gotoRowByFirstColumn(DatasetState& dataset, const std::wstring& key);
    void undo(DatasetState& dataset);
    void redo(DatasetState& dataset);
    void addColumn(DatasetState& dataset, const std::wstring& columnName, int insertAfter);
    void deleteColumn(DatasetState& dataset, int columnIndex);
    void renameColumn(DatasetState& dataset, int columnIndex, const std::wstring& newName);
    void moveColumn(DatasetState& dataset, int fromIndex, int toIndex);
    void bulkSetColumnValue(DatasetState& dataset, int columnIndex, const std::wstring& value, bool visibleRowsOnly);
    void bulkReplaceColumnValue(DatasetState& dataset, int columnIndex, const std::wstring& findValue, const std::wstring& replaceValue, bool visibleRowsOnly);
    bool confirmDiscardChanges(const DatasetState& dataset, const wchar_t* action) const;
    bool hasActiveDataset() const;
    DatasetState& activeDataset();
    const DatasetState& activeDataset() const;
    DatasetState& datasetByKind(DatasetKind kind);
    void markModified(DatasetState& dataset, size_t sourceRow, int column);
    void clearModified(DatasetState& dataset);
    const char* tr(const char* english, const char* turkish) const;
    std::string trs(const std::string& english, const std::string& turkish) const;
    std::wstring trw(const wchar_t* english, const wchar_t* turkish) const;

    std::wstring findConfigPath(DatasetKind kind) const;
    std::wstring detectDatasetPath(DatasetKind kind) const;
    std::wstring openFileDialog(const wchar_t* title, const wchar_t* filter) const;
    std::wstring saveFileDialog(const wchar_t* title, const std::wstring& defaultPath, const wchar_t* filter) const;

    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT handleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    HINSTANCE instance_ = nullptr;
    HWND hwnd_ = nullptr;

    ID3D11Device* device_ = nullptr;
    ID3D11DeviceContext* deviceContext_ = nullptr;
    IDXGISwapChain* swapChain_ = nullptr;
    ID3D11RenderTargetView* renderTargetView_ = nullptr;

    AppPreferences preferences_;
    DatasetState datasets_[2];
    int activeDatasetIndex_ = 0;
    bool running_ = true;

    std::string editBuffer_;
    std::wstring editBufferWide_;
    size_t editSourceRow_ = 0;
    int editColumn_ = -1;
    bool editModalOpen_ = false;

    std::vector<std::wstring> flagSelection_;
    std::string flagSearch_;
    std::wstring flagOriginalValue_;
    size_t flagSourceRow_ = 0;
    int flagColumn_ = -1;
    bool flagModalOpen_ = false;

    std::string statusText_ = "Ready";
    bool requestScrollToSelection_ = false;
    bool aboutModalOpen_ = false;
    bool gotoRowModalOpen_ = false;
    bool columnManagerModalOpen_ = false;
    bool columnOrderModalOpen_ = false;
    bool exportModalOpen_ = false;
    std::string columnNameBuffer_;
    int columnTargetIndex_ = -1;
    int columnAction_ = 0;
    bool bulkEditModalOpen_ = false;
    int bulkEditMode_ = 0;
    std::string bulkValueBuffer_;
    std::string bulkFindBuffer_;
    std::string bulkReplaceBuffer_;
    bool bulkVisibleRowsOnly_ = true;
    std::string exportFormat_ = "csv";
    bool showValidationPanel_ = false;
    bool showHistoryPanel_ = false;
    bool showSearchPanel_ = false;
    bool showComparePanel_ = false;
    bool showLinkedNamesPanel_ = false;
    bool showWorkspacePanel_ = false;
    bool showRulePresetPanel_ = false;
    bool showThemeBuilder_ = false;
};

#endif
