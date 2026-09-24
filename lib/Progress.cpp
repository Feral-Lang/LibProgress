#include "Progress.hpp"

#include <chrono>

namespace fer
{

Mutex gMtx;
Vector<VarProgressBar *> gBars;

constexpr size_t MAX_BAR_SIZE          = 100;
constexpr const char ANSI_MOVE_UP[]    = "\033[1F";
constexpr const char ANSI_MOVE_DOWN[]  = "\033[1E";
constexpr const char ANSI_CLEAR_LINE[] = "\033[2K";
constexpr const char ANSI_MOVE_BEGIN[] = "\r";
// set by updateAllNative
size_t gLineWidth            = 0;
char *gWriteBuf              = nullptr;
Atomic<int64_t> gLastUpdated = 0;
int64_t gUpdateIntervalMs =
#if defined(CORE_OS_WINDOWS)
    50;
#else
    10;
#endif

void removeBar(VarProgressBar *bar)
{
    for(auto it = gBars.begin(); it != gBars.end(); ++it) {
        if(*it == bar) {
            gBars.erase(it);
            break;
        }
    }
}

void updateAllBars(VarProgressBar *finalized = nullptr)
{
    // no need to update more frequently than once in 5 ms
    if(!gBars.empty()) {
        for(size_t i = 0; i < gBars.size(); ++i) {
            if(!gBars[i]->startedRendering()) continue;
            fwrite(ANSI_MOVE_UP, 1, sizeof(ANSI_MOVE_UP) - 1, stdout);
        }
    }
    String writeBuf(gLineWidth, ' ');
    if(finalized) {
        fwrite(ANSI_MOVE_BEGIN, 1, sizeof(ANSI_MOVE_BEGIN) - 1, stdout);
        fwrite(ANSI_CLEAR_LINE, 1, sizeof(ANSI_CLEAR_LINE) - 1, stdout);
        finalized->renderBar(gLineWidth);
        fputc('\n', stdout);
        removeBar(finalized);
    }
    for(size_t i = 0; i < gBars.size(); ++i) {
        fwrite(ANSI_MOVE_BEGIN, 1, sizeof(ANSI_MOVE_BEGIN) - 1, stdout);
        fwrite(ANSI_CLEAR_LINE, 1, sizeof(ANSI_CLEAR_LINE) - 1, stdout);
        gBars[i]->renderBar(gLineWidth);
        fputc('\n', stdout);
    }
}

//////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////// VarProgressBar ///////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////

VarProgressBar::VarProgressBar(ModuleLoc loc, StringRef name, char emptyChar, char filledChar,
                               char currentChar)
    : Var(loc), name(name), currentPercent(0), emptyChar(emptyChar), filledChar(filledChar),
      currentChar(currentChar), hasStartedRendering(false)
{}
VarProgressBar::~VarProgressBar() {}

void VarProgressBar::onCreate(VirtualMachine &vm)
{
    LockGuard<Mutex> _(gMtx);
    gBars.push_back(this);
}
void VarProgressBar::onDestroy(VirtualMachine &vm)
{
    LockGuard<Mutex> _(gMtx);
    updateAllBars(this);
}

void VarProgressBar::renderBar(size_t lineWidth)
{
    hasStartedRendering = true;
    size_t nameLen      = name.size();
    if(!name.empty()) {
        fwrite(name.c_str(), 1, name.size(), stdout);
        // at least 2 spaces between name and the bar
        if(lineWidth <= name.size() + 2) return;
        nameLen += 2;
    }
    // 7 for '[', ']', ' ', 'x', 'x', 'x', '%'
    int64_t remainingSpace = (int64_t)lineWidth - (int64_t)nameLen - 7;
    if(remainingSpace < 0) return;
    size_t barSize = remainingSpace >= MAX_BAR_SIZE ? MAX_BAR_SIZE : remainingSpace;
    if(!name.empty()) {
        fwrite("  ", 1, 2, stdout);
        // bar should be right aligned IF name is not empty
        if(remainingSpace > barSize) {
            size_t emptySpace = remainingSpace - barSize;
            memset(gWriteBuf, ' ', emptySpace);
            fwrite(gWriteBuf, 1, emptySpace, stdout);
        }
    }
    fputc('[', stdout);
    size_t currVal = ((float)currentPercent / 100.f) * (float)barSize;
    if(currVal > barSize) currVal = barSize;
    size_t remVal = barSize - currVal;
    if(currVal > 0) {
        if(currVal > 1) {
            memset(gWriteBuf, filledChar, currVal - 1);
            fwrite(gWriteBuf, 1, currVal - 1, stdout);
        }
        fwrite(&currentChar, 1, 1, stdout);
    }
    memset(gWriteBuf, emptyChar, remVal);
    fwrite(gWriteBuf, 1, remVal, stdout);
    fwrite("] ", 1, 2, stdout);
    if(currentPercent < 100) fputc(' ', stdout);
    if(currentPercent < 10) fputc(' ', stdout);
    fprintf(stdout, "%zu%%", currentPercent);
}

//////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////// Functions /////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////

FERAL_FUNC(setUpdateInterval, 1, false,
           "  fn(intervalMs) -> Nil\n"
           "Set the update interval in milliseconds for progress bars' rendering.")
{
    EXPECT(VarInt, args[1], "interval in ms");
    gUpdateIntervalMs = as<VarInt>(args[1])->getVal();
    return vm.getNil();
}

FERAL_FUNC(newBarNative, 4, false,
           "  fn(name, emptyChar, filledChar, currentChar) -> ProgressBar\n"
           "Create and return a new prgress bar.")
{
    EXPECT(VarStr, args[1], "name for the progress bar");
    EXPECT(VarStr, args[2], "empty char for the progress bar");
    EXPECT(VarStr, args[3], "filled char for the progress bar");
    EXPECT(VarStr, args[4], "current char for the progress bar");

    StringRef name   = as<VarStr>(args[1])->getVal();
    char emptyChar   = as<VarStr>(args[2])->getVal()[0];
    char filledChar  = as<VarStr>(args[3])->getVal()[0];
    char currentChar = as<VarStr>(args[4])->getVal()[0];

    return vm.makeVar<VarProgressBar>(loc, name, emptyChar, filledChar, currentChar);
}

FERAL_FUNC(updateAllNative, 1, false,
           "  fn(lineWidth) -> Nil\n"
           "Updates (renders) all the progress bars.")
{
    EXPECT(VarInt, args[1], "line width");
    size_t lineWidth    = as<VarInt>(args[1])->getVal();
    int64_t currentTime = std::chrono::duration_cast<std::chrono::milliseconds>(
                              std::chrono::system_clock::now().time_since_epoch())
                              .count();
    LockGuard<Mutex> _(gMtx);
    if(gLineWidth != lineWidth) {
        gLineWidth = lineWidth;
        if(gWriteBuf) free(gWriteBuf);
        gWriteBuf = (char *)malloc(sizeof(*gWriteBuf) * gLineWidth);
    }
    if(currentTime - gLastUpdated < gUpdateIntervalMs) return vm.getNil();
    updateAllBars();
    gLastUpdated = std::chrono::duration_cast<std::chrono::milliseconds>(
                       std::chrono::system_clock::now().time_since_epoch())
                       .count();
    return vm.getNil();
}

FERAL_FUNC(barUpdateNative, 1, false,
           "  var.fn(percent) -> Nil\n"
           "Set the full percentage for the progress bar `var`.")
{
    EXPECT(VarInt, args[1], "new percentage value for the progress bar");
    as<VarProgressBar>(args[0])->updatePercent(as<VarInt>(args[1])->getVal());
    return vm.getNil();
}

FERAL_FUNC(barSetName, 1, false,
           "  var.fn(newName) -> Nil\n"
           "Set/change the name of the bar `var` as `newName`.\n"
           "Returns `nil`.")
{
    EXPECT(VarStr, args[1], "new name");
    as<VarProgressBar>(args[0])->setName(as<VarStr>(args[1])->getVal());
    return vm.getNil();
}

FERAL_FUNC(barGetName, 0, false,
           "  var.fn() -> Str\n"
           "Returns the name of the bar `var` as a String.")
{
    return vm.makeVar<VarStr>(loc, as<VarProgressBar>(args[0])->getName());
}

INIT_DLL(Progress)
{
    // Register the type names
    vm.addLocalType<VarProgressBar>(loc, "ProgressBar", "Progress bar type.");

    vm.addLocal(loc, "setUpdateInterval", setUpdateInterval);
    vm.addLocal(loc, "newBarNative", newBarNative);
    vm.addLocal(loc, "updateAllNative", updateAllNative);

    vm.addTypeFn<VarProgressBar>(loc, "setName", barSetName);
    vm.addTypeFn<VarProgressBar>(loc, "getName", barGetName);
    vm.addTypeFn<VarProgressBar>(loc, "updateNative", barUpdateNative);
    return true;
}

DEINIT_DLL(Progress)
{
    if(gWriteBuf) free(gWriteBuf);
}

} // namespace fer