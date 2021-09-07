#include <library.h>
#include <Arduino.h>
#include "../../../os_host/source/framework/Console.h"
#include "../../../os_host/source/framework/SimpleApp.h"
#include "terminal-basic/basic_interpreter.hpp"

class Output : public VT100::Print
{
public:
    Output() : VT100::Print() {}
    void clear() override {}
    
protected:
    uint8_t getCursorX() override { return 0; }
    void setCursor(uint8_t, uint8_t) override {}
    void setCursorX(uint8_t) override {}
    void writeChar(uint8_t c) override {assert(0);}
    
    virtual void addAttribute(VT100::TextAttr) override {assert(0);}
    virtual void resetAttributes() override {assert(0);}

    virtual size_t write(uint8_t c) override {
        static char c0, c1, c2, c3;
        c0 = c1; c1 = c2; c2 = c3; c3 = c;
        if (c0 == 27 || c1 == 27 || c2 == 27 || c3 == 27)
        {
            if (c0 == 27)
            {
                if (c1 == '[' && c2 == '1' && c3 == 'm')
                    CONSOLE::Color(RGB565(ffff50));
                else if (c1 == '[' && c2 == '0' && c3 == 'm')
                    CONSOLE::Color(RGB565(ffffff));
                else
                    assert(0);
            }
            return 0;
        }
        CONSOLE::Putch(c);        
        return 0;
    }

private:
    void scroll() {}
};

uint32_t HAL_random_generate(uint32_t max) { return rand() % max; }
void HAL_random_seed(uint32_t seed) { srand(seed); }
uint32_t HAL_time_gettime_ms() { return BIOS::SYS::GetTick(); }
void HAL_time_sleep_ms(uint32_t ms) { BIOS::SYS::DelayMs(ms); }
void HAL_update() { }

bool setup()
{
    GUI::Background(CRect(0, 14, BIOS::LCD::Width, BIOS::LCD::Height-14), RGB565(0000b0), RGB565(4040d0));

    return true;
}

#ifdef _ARM
__attribute__((__section__(".entry")))
#endif
int _main(void)
{
    CONSOLE::colorBack = RGB565(202020);

    APP::Init("BASIC Interpreter");
    APP::Status("");

    Stream _stream;
    Print _print;
    Output _output;
    BASIC::Interpreter basic(_stream, _output, BASIC::SINGLE_PROGSIZE);
    basic.newProgram();
    
    if (setup())
    {
        BIOS::KEY::EKey key;
        while ((key = BIOS::KEY::GetKey()) != BIOS::KEY::EKey::Escape)
        {
            basic.step();
        }
    }
    return 0;
}

void _HandleAssertion(const char* file, int line, const char* cond)
{
    BIOS::DBG::Print("Assertion failed in ");
    BIOS::DBG::Print(file);
    BIOS::DBG::Print(" [%d]: %s\n", line, cond);
    while (1);
}

#ifndef __APPLE__

extern "C" void __cxa_pure_virtual(void)
{
  _ASSERT(!"Pure virtual call");
}
#endif
