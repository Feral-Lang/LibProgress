#pragma once

#include <VM/VM.hpp>

namespace fer
{

//////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////// VarProgresBar ///////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////

class VarProgressBar : public Var
{
    String name;
    size_t currentPercent;
    char emptyChar;
    char filledChar;
    char currentChar;
    bool hasStartedRendering;

    void onCreate(VirtualMachine &vm) override;
    void onDestroy(VirtualMachine &vm) override;

public:
    // By default, currentChar = filledChar
    VarProgressBar(ModuleLoc loc, StringRef name, char emptyChar, char filledChar,
                   char currentChar);
    ~VarProgressBar();

    void renderBar(size_t lineWidth);

    inline void updatePercent(size_t value) { currentPercent = value; }
    inline bool startedRendering() { return hasStartedRendering; }
};

} // namespace fer