#include "Progress.hpp"

#include <chrono>

namespace fer
{

Mutex gMtx;
Vector<VarProgressBar *> gBars;

constexpr size_t MAX_BAR_SIZE         = 100;
constexpr const char *ANSI_MOVE_UP    = "\033[1F";
constexpr const char *ANSI_MOVE_DOWN  = "\033[1E";
constexpr const char *ANSI_CLEAR_LINE = "\033[2K";
constexpr const char *ANSI_MOVE_BEGIN = "\r";
// set by updateAllNative
size_t gLineWidth            = 0;
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
            std::cout << ANSI_MOVE_UP;
        }
    }
    if(finalized) {
        std::cout << ANSI_MOVE_BEGIN << ANSI_CLEAR_LINE;
        finalized->renderBar(gLineWidth);
        std::cout << "\n";
        removeBar(finalized);
    }
    for(size_t i = 0; i < gBars.size(); ++i) {
        std::cout << ANSI_MOVE_BEGIN << ANSI_CLEAR_LINE;
        gBars[i]->renderBar(gLineWidth);
        std::cout << "\n";
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
        std::cout << name;
        // at least 2 spaces between name and the bar
        if(lineWidth <= name.size() + 2) return;
        nameLen += 2;
    }
    // 7 for '[', ']', ' ', 'x', 'x', 'x', '%'
    int64_t remainingSpace = (int64_t)lineWidth - (int64_t)nameLen - 7;
    if(remainingSpace < 0) return;
    size_t barSize = remainingSpace >= MAX_BAR_SIZE ? MAX_BAR_SIZE : remainingSpace;
    if(!name.empty()) {
        std::cout << "  ";
        // bar should be right aligned IF name is not empty
        if(remainingSpace > barSize) {
            size_t emptySpace = remainingSpace - barSize;
            for(size_t i = 0; i < emptySpace; ++i) std::cout << ' ';
        }
    }
    std::cout << "[";
    size_t currVal = ((float)currentPercent / 100.f) * (float)barSize;
    size_t remVal  = barSize - currVal;
    for(size_t i = 0; i < currVal; ++i) {
        if(i == currVal - 1) std::cout << currentChar;
        else std::cout << filledChar;
    }
    for(size_t i = 0; i < remVal; ++i) std::cout << emptyChar;
    std::cout << "]";
    std::cout << " ";
    if(currentPercent < 100) std::cout << " ";
    if(currentPercent < 10) std::cout << " ";
    std::cout << currentPercent << "%";
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
    gLineWidth = lineWidth;
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

INIT_DLL(Progress)
{
    // Register the type names
    vm.addLocalType<VarProgressBar>(loc, "ProgressBar", "Progress bar type.");

    vm.addLocal(loc, "setUpdateInterval", setUpdateInterval);
    vm.addLocal(loc, "newBarNative", newBarNative);
    vm.addLocal(loc, "updateAllNative", updateAllNative);

    vm.addTypeFn<VarProgressBar>(loc, "updateNative", barUpdateNative);
    return true;
}

} // namespace fer