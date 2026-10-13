*** Comments ***
InkTime firmware on an emulated nRF52840 (Renode).

The ELF under test is the real ARM build; the peripherals are the C# models in
InkTimePeripherals.cs. Panel contents are compared byte-for-byte with the golden
images produced by the host simulator, so both execution paths must agree.

Run with:  make renode-test


*** Settings ***
Library             OperatingSystem
Resource            ${RENODEKEYWORDS}

Suite Setup         InkTime Suite Setup
Suite Teardown      Teardown
Test Setup          Create InkTime
Test Teardown       Test Teardown


*** Variables ***
${FW_ROOT}          ${CURDIR}/../..
${FW_ELF}           ${FW_ROOT}/build/fw/inktime.elf
${GOLDEN}           ${FW_ROOT}/tests/golden
${UART}             sysbus.uart0
${EPD}              sysbus.spi2.epd
${BMA}              sysbus.twi0.bma
${FUEL}             sysbus.twi0.fuel
${CHARGER}          sysbus.twi0.charger
${HAPTIC}           sysbus.twi0.haptic


*** Keywords ***
InkTime Suite Setup
    Setup
    Execute Command    include @${CURDIR}/InkTimePeripherals.cs

Create InkTime
    Reset Emulation
    Execute Command    mach create "inktime"
    # Jump over WFI sleep instead of waiting in host time.
    Execute Command    emulation SetGlobalAdvanceImmediately true
    Execute Command    machine LoadPlatformDescription @${CURDIR}/inktime.repl
    Execute Command    sysbus LoadELF @${FW_ELF}
    Create Terminal Tester    ${UART}    defaultPauseEmulation=True

Boot To Watchface
    Start Emulation
    Wait For Line On Uart    hw: epd ok imu ok fg ok chg ok hap ok    timeout=10
    Wait For Line On Uart    display: full refresh    timeout=10
    Run For    2

Run For
    [Arguments]    ${seconds}
    Execute Command    emulation RunFor "${seconds}"

Press
    [Arguments]    ${button}    ${hold}=0.12
    ${port}=    Set Variable If    '${button}' == 'btn_down'    gpio1    gpio0
    Execute Command    sysbus.${port}.${button} Press
    Run For    ${hold}
    Execute Command    sysbus.${port}.${button} Release
    Run For    0.1

Property Should Be
    [Arguments]    ${property}    ${expected}
    ${value}=    Execute Command    ${property}
    # The monitor prints integers in hex (0x00000001); compare numerically when possible.
    IF    $expected.isdigit()
        Should Be Equal As Integers    ${value.strip()}    ${expected}
    ELSE
        Should Be Equal As Strings    ${value.strip()}    ${expected}
    END

Panel Should Match Golden
    [Arguments]    ${name}
    ${actual}=    Set Variable    ${OUTPUT DIR}/renode_${name}
    Execute Command    ${EPD} SavePbm @${actual}
    ${a}=    Get Binary File    ${actual}
    ${g}=    Get Binary File    ${GOLDEN}/${name}
    Should Be Equal    ${a}    ${g}    Panel differs from host golden ${name} (see ${actual})    values=False


*** Test Cases ***
Boot Detects All Peripherals
    Start Emulation
    Wait For Line On Uart    InkTime fw 1.0.0 booting    timeout=5
    Wait For Line On Uart    hw: epd ok imu ok fg ok chg ok hap ok    timeout=10
    Property Should Be    ${BMA} EngineOk    True
    Property Should Be    ${BMA} StepCounterEnabled    True
    Property Should Be    ${HAPTIC} ErmOpenLoop    True
    Property Should Be    ${CHARGER} ChargeCurrentMa    120
    Property Should Be    ${CHARGER} RegulationMv    4200
    Property Should Be    ${CHARGER} WatchdogDisabled    True
    Property Should Be    ${FUEL} AlertThreshold    5

Boot Screen Matches Host Simulator
    Boot To Watchface
    Property Should Be    ${EPD} FullRefreshes    1
    Property Should Be    ${EPD} Errors    0
    Property Should Be    ${EPD} Sleeping    True
    Panel Should Match Golden    boot.pbm

Enter Opens Menu With Haptic Click
    Boot To Watchface
    Press    btn_enter
    Wait For Line On Uart    button: enter short    timeout=2
    Wait For Line On Uart    screen: menu    timeout=2
    Wait For Line On Uart    display: partial refresh    timeout=2
    Run For    1
    Property Should Be    ${HAPTIC} LastEffect    1
    Property Should Be    ${HAPTIC} Errors    0
    Property Should Be    ${HAPTIC} Standby    True
    Panel Should Match Golden    menu.pbm

Step Counter Interrupt
    Boot To Watchface
    Execute Command    ${BMA} AddSteps 45
    Wait For Line On Uart    steps: 45    timeout=2

Charger Interrupt
    Boot To Watchface
    Execute Command    ${CHARGER} SetCharger true 1
    Wait For Line On Uart    power: 87% 3950mV usb=1 charging=1 full=0    timeout=2

Fuel Gauge Alert Shows Low Battery
    Boot To Watchface
    Execute Command    ${FUEL} SetBattery 4 3550
    Wait For Line On Uart    power: fuel gauge alert    timeout=2
    Wait For Line On Uart    screen: lowbatt    timeout=2
    Run For    2
    Property Should Be    ${FUEL} AlertActive    False
    Panel Should Match Golden    low_battery.pbm

Minute Tick Triggers One Partial Refresh
    Boot To Watchface
    Run For    61
    Property Should Be    ${EPD} PartialRefreshes    1
    Property Should Be    ${EPD} FullRefreshes    1
    Property Should Be    ${EPD} Errors    0

Power Off From Settings
    Boot To Watchface
    Press    btn_enter
    Press    btn_up
    Press    btn_up
    Press    btn_enter
    Press    btn_up
    Wait For Line On Uart    screen: settings    timeout=2
    Press    btn_enter
    Wait For Line On Uart    screen: off    timeout=2
    Wait For Line On Uart    power: system off    timeout=5
    Panel Should Match Golden    power_off.pbm

Watchdog Stays Quiet For Ten Minutes
    Boot To Watchface
    Run For    600
    Run Keyword And Expect Error    *    Wait For Line On Uart    booting    timeout=1
    Property Should Be    ${EPD} Errors    0

Rev 1 Firmware Ignores The Unrouted Down Button
    # BOARD_REV=1: P1.02 is shorted to the EPD power switch on the real PCB, so it is not used.
    Boot To Watchface
    Press    btn_down
    Run Keyword And Expect Error    *    Wait For Line On Uart    button: down    timeout=1
    # A timed-out wait leaves the emulation running; pause it so RunFor works again.
    Execute Command    pause
    Press    btn_up
    Wait For Line On Uart    button: up short    timeout=2
