// Renode models of the InkTime peripherals. Loaded at runtime with `include @InkTimePeripherals.cs`.
// They mirror the C models in sim/models/ so the same firmware behaviour is checked
// on the emulated nRF52840 (real ELF, real register accesses).
using System;
using System.Collections.Generic;
using System.IO;
using System.Text;
using Antmicro.Renode.Core;
using Antmicro.Renode.Logging;
using Antmicro.Renode.Peripherals.I2C;
using Antmicro.Renode.Peripherals.SPI;
using Antmicro.Renode.Peripherals.Timers;
using Antmicro.Renode.Time;

namespace Antmicro.Renode.Peripherals.InkTime
{
    // ------------------------------------------------------------------ SSD1681 e-paper
    // GPIO inputs: 0 = DC, 1 = RST, 2 = CS, 3 = PWR_N. Output: Busy.
    public class SSD1681 : ISPIPeripheral, IGPIOReceiver
    {
        public SSD1681(IMachine machine)
        {
            Busy = new GPIO();
            timer = new LimitTimer(machine.ClockSource, 1000, this, "busy", limit: 1, direction: Direction.Ascending,
                                   enabled: false, workMode: WorkMode.OneShot, eventEnabled: true);
            timer.LimitReached += () => { busyTimer = false; UpdateBusy(); };
            Reset();
        }

        public GPIO Busy { get; }
        public int FullRefreshes { get; private set; }
        public int PartialRefreshes { get; private set; }
        public int Sleeps { get; private set; }
        public int Errors { get; private set; }
        public string LastError { get; private set; } = "";
        public bool Sleeping => sleeping;

        public void Reset()
        {
            dc = false; cs = true; rstLow = false;
            powered = true; sleeping = false; busyTimer = false;
            FullRefreshes = 0; PartialRefreshes = 0; Sleeps = 0; Errors = 0; LastError = "";
            for(var i = 0; i < Bytes; i++) { bw[i] = 0xFF; red[i] = 0xFF; glass[i] = 0xFF; }
            ResetRegisters();
            timer.Enabled = false;
            UpdateBusy();
        }

        public void OnGPIO(int number, bool value)
        {
            switch(number)
            {
            case 0: dc = value; break;
            case 1:
                if(!value) { rstLow = true; }
                else if(rstLow) { rstLow = false; sleeping = false; ResetRegisters(); StartBusy(2); }
                break;
            case 2: cs = value; break;
            case 3: powered = !value; if(!powered) { sleeping = true; } break;
            }
            UpdateBusy();
        }

        public byte Transmit(byte data)
        {
            if(!powered) { Error("SPI traffic while panel unpowered"); return 0; }
            if(cs) { Error("SPI traffic with CS high"); return 0; }
            if(dc) { Data(data); } else { Command(data); }
            UpdateBusy();
            return 0;
        }

        public void FinishTransmission() { }

        // Panel image as P4 PBM (1 = black), same format as the host simulator's golden files.
        public void SavePbm(string path)
        {
            using(var f = File.Create(path))
            {
                var header = Encoding.ASCII.GetBytes("P4\n200 200\n");
                f.Write(header, 0, header.Length);
                var inv = new byte[Bytes];
                for(var i = 0; i < Bytes; i++) { inv[i] = (byte)~glass[i]; }
                f.Write(inv, 0, inv.Length);
            }
        }

        public int BlackPixels()
        {
            var n = 0;
            foreach(var b in glass) { for(var v = (byte)~b; v != 0; v &= (byte)(v - 1)) { n++; } }
            return n;
        }

        private void Error(string msg) { Errors++; LastError = msg; this.Log(LogLevel.Warning, "SSD1681: {0}", msg); }

        private void ResetRegisters()
        {
            cmd = 0; idx = 0; ctrl2 = 0; mux = 0;
            xStart = 0; xEnd = 24; yStart = 0; yEnd = 199; xCnt = 0; yCnt = 0;
        }

        private bool IsBusy => sleeping || rstLow || busyTimer;

        private void StartBusy(ulong ms)
        {
            busyTimer = true;
            timer.Enabled = false;
            timer.Value = 0;
            timer.Limit = ms;
            timer.Enabled = true;
        }

        private void UpdateBusy()
        {
            Busy.Set(powered && IsBusy);
        }

        private void Command(byte c)
        {
            if(sleeping) { Error("command while in deep sleep"); return; }
            if(busyTimer) { Error("command while BUSY"); }
            cmd = c; idx = 0;
            if(c == 0x12) { ResetRegisters(); StartBusy(2); }
            else if(c == 0x20) { Activate(); }
        }

        private void Activate()
        {
            if(ctrl2 == 0) { Error("master activation without display update control"); return; }
            if((ctrl2 & 0x04) == 0) { StartBusy(5); return; }
            if(mux != 199) { Error("driver output control not set to 200 gates"); }
            if((ctrl2 & 0x08) != 0)
            {
                for(var i = 0; i < Bytes; i++)
                {
                    var changed = (byte)(bw[i] ^ red[i]);
                    glass[i] = (byte)((glass[i] & ~changed) | (bw[i] & changed));
                }
                PartialRefreshes++;
                StartBusy(PartialMs);
            }
            else
            {
                Array.Copy(bw, glass, Bytes);
                FullRefreshes++;
                StartBusy(FullMs);
            }
        }

        private void RamWrite(byte[] ram, byte b)
        {
            if(xCnt >= 25 || yCnt >= 200) { Error("RAM write outside panel"); return; }
            ram[yCnt * 25 + xCnt] = b;
            if(++xCnt > xEnd) { xCnt = xStart; if(++yCnt > yEnd) { yCnt = yStart; } }
        }

        private void Data(byte b)
        {
            if(sleeping) { Error("data while in deep sleep"); return; }
            if(busyTimer) { Error("data while BUSY"); }
            var i = idx++;
            switch(cmd)
            {
            case 0x01:
                if(i == 0) { mux = (mux & 0x100) | b; }
                if(i == 1) { mux = (mux & 0xFF) | ((b & 1) << 8); }
                break;
            case 0x10: if(i == 0 && b != 0) { sleeping = true; Sleeps++; } break;
            case 0x22: ctrl2 = b; break;
            case 0x24: RamWrite(bw, b); break;
            case 0x26: RamWrite(red, b); break;
            case 0x44: if(i == 0) { xStart = b; } if(i == 1) { xEnd = b; } break;
            case 0x45:
                if(i == 0) { yStart = (yStart & 0x100) | b; }
                if(i == 1) { yStart = (yStart & 0xFF) | ((b & 1) << 8); }
                if(i == 2) { yEnd = (yEnd & 0x100) | b; }
                if(i == 3) { yEnd = (yEnd & 0xFF) | ((b & 1) << 8); }
                break;
            case 0x4E: if(i == 0) { xCnt = b; } break;
            case 0x4F:
                if(i == 0) { yCnt = (yCnt & 0x100) | b; }
                if(i == 1) { yCnt = (yCnt & 0xFF) | ((b & 1) << 8); }
                break;
            }
        }

        private const int Bytes = 5000;
        private const ulong FullMs = 1800;
        private const ulong PartialMs = 400;

        private readonly LimitTimer timer;
        private readonly byte[] bw = new byte[Bytes];
        private readonly byte[] red = new byte[Bytes];
        private readonly byte[] glass = new byte[Bytes];
        private bool dc, cs, rstLow, powered, sleeping, busyTimer;
        private byte cmd, ctrl2;
        private int idx, mux, xStart, xEnd, yStart, yEnd, xCnt, yCnt;
    }

    // Base class for byte-register I2C devices with an auto-incrementing pointer.
    // Renode delivers a write transaction as one Write() at STOP / repeated START,
    // and reads one byte per Read() call.
    public abstract class RegisterI2C : II2CPeripheral
    {
        public virtual void Reset() { pointer = 0; readIndex = 0; }

        public void Write(byte[] data)
        {
            if(data.Length == 0) { return; }
            pointer = data[0];
            readIndex = 0;
            for(var i = 1; i < data.Length; i++) { WriteByte(pointer, i - 1, data[i]); if(!IsPort(pointer)) { pointer++; } }
        }

        public byte[] Read(int count = 1)
        {
            var result = new byte[count];
            for(var i = 0; i < count; i++)
            {
                result[i] = ReadByte(pointer, readIndex++);
                if(!IsPort(pointer)) { pointer++; }
            }
            return result;
        }

        public void FinishTransmission() { }

        protected virtual bool IsPort(byte reg) => false;
        protected abstract void WriteByte(byte reg, int index, byte value);
        protected abstract byte ReadByte(byte reg, int index);

        protected byte pointer;
        protected int readIndex;
    }

    // ------------------------------------------------------------------ BMA423
    public class BMA423 : RegisterI2C
    {
        public BMA423() { Int1 = new GPIO(); Reset(); }

        public GPIO Int1 { get; }
        public uint Steps { get; private set; }
        public bool EngineOk { get; private set; }
        public uint ConfigCrc { get; private set; }
        public int Errors { get; private set; }
        public bool StepCounterEnabled => EngineOk && (feature[0x3B] & 0x10) != 0 && (regs[0x7D] & 0x04) != 0;

        public override void Reset()
        {
            base.Reset();
            Array.Clear(regs, 0, regs.Length);
            Array.Clear(config, 0, config.Length);
            Array.Clear(feature, 0, feature.Length);
            regs[0x00] = 0x13; regs[0x40] = 0xA8; regs[0x7C] = 0x03;
            configBytes = 0; apsViolation = false; asicWord = 0; EngineOk = false; intStatus0 = 0;
            UpdateInt();
        }

        public void AddSteps(uint n)
        {
            if(!StepCounterEnabled) { this.Log(LogLevel.Warning, "BMA423: step counter not enabled"); return; }
            var wm = (uint)(feature[0x3A] | ((feature[0x3B] & 0x03) << 8));
            var before = Steps;
            Steps += n;
            if(wm != 0 && Steps / (wm * 20) != before / (wm * 20)) { intStatus0 |= 0x02; UpdateInt(); }
        }

        protected override bool IsPort(byte reg) => reg == 0x5E;

        protected override void WriteByte(byte reg, int index, byte v)
        {
            switch(reg)
            {
            case 0x5E:
                if((regs[0x7C] & 1) != 0) { apsViolation = true; Errors++; }
                if(!EngineOk)
                {
                    var off = asicWord * 2 + index;
                    if(off < ConfigSize) { config[off] = v; configBytes++; } else { Errors++; }
                }
                else if(asicWord == FeatStart && index < FeatSize) { feature[index] = v; }
                else { Errors++; }
                break;
            case 0x5B: asicWord = (asicWord & 0xFF0) | (v & 0x0F); break;
            case 0x5C: asicWord = (asicWord & 0x00F) | (v << 4); break;
            case 0x59:
                regs[0x59] = v;
                if(v == 1)
                {
                    ConfigCrc = Crc32(config);
                    EngineOk = configBytes >= ConfigSize && !apsViolation && ConfigCrc == ExpectedCrc;
                    regs[0x2A] = (byte)(EngineOk ? 0x01 : 0x02);
                    if(EngineOk) { asicWord = FeatStart; }
                }
                break;
            case 0x7E: if(v == 0xB6) { var s = Steps; Reset(); Steps = s; } break;
            default: if(reg != 0x00 && reg != 0x2A && reg < 0x80) { regs[reg] = v; } break;
            }
            UpdateInt();
        }

        protected override byte ReadByte(byte reg, int index)
        {
            switch(reg)
            {
            case 0x17: return 0x40; // Z = +1 g
            case 0x1C: { var v = intStatus0; intStatus0 = 0; UpdateInt(); return v; }
            case 0x1E: return (byte)Steps;
            case 0x1F: return (byte)(Steps >> 8);
            case 0x20: return (byte)(Steps >> 16);
            case 0x21: return (byte)(Steps >> 24);
            case 0x5B: return (byte)(asicWord & 0x0F);
            case 0x5C: return (byte)(asicWord >> 4);
            case 0x5E:
                if(EngineOk) { return (asicWord == FeatStart && index < FeatSize) ? feature[index] : (byte)0; }
                var off = asicWord * 2 + index;
                return off < ConfigSize ? config[off] : (byte)0;
            default: return reg < 0x80 && (reg < 0x12 || reg > 0x17) ? regs[reg] : (byte)0;
            }
        }

        private void UpdateInt()
        {
            var io = regs[0x53];
            var active = (intStatus0 & regs[0x56]) != 0;
            var activeHigh = (io & 0x02) != 0;
            Int1.Set((io & 0x08) != 0 && active == activeHigh);
        }

        private static uint Crc32(byte[] data)
        {
            var crc = 0xFFFFFFFFu;
            foreach(var b in data)
            {
                crc ^= b;
                for(var k = 0; k < 8; k++) { crc = (crc & 1) != 0 ? (crc >> 1) ^ 0xEDB88320u : crc >> 1; }
            }
            return ~crc;
        }

        private const int ConfigSize = 6144;
        private const uint ExpectedCrc = 0xE08E8A46; // CRC-32 of Bosch bma423_config_file[]
        private const int FeatStart = 0x0400;
        private const int FeatSize = 70;
        private readonly byte[] regs = new byte[0x80];
        private readonly byte[] config = new byte[ConfigSize];
        private readonly byte[] feature = new byte[FeatSize];
        private int configBytes;
        private bool apsViolation;
        private int asicWord;
        private byte intStatus0;
    }

    // ------------------------------------------------------------------ MAX17048 (16-bit big-endian)
    public class MAX17048 : RegisterI2C
    {
        public MAX17048() { Alert = new GPIO(); Reset(); }

        public GPIO Alert { get; } // driven high when released (pull-up), low when asserted

        public override void Reset()
        {
            base.Reset();
            Array.Clear(mem, 0, mem.Length);
            Set16(0x08, 0x0012); Set16(0x0A, 0x8030); Set16(0x0C, 0x971C);
            Set16(0x14, 0x00FF); Set16(0x18, 0x9600); Set16(0x1A, 0x0100);
            SetBattery(87, 3950);
        }

        public int AlertThreshold => 32 - (mem[0x0D] & 0x1F);
        public bool AlertActive => (mem[0x0D] & 0x20) != 0;

        public void SetBattery(int pct, int mv)
        {
            var old = mem[0x04];
            Set16(0x02, (ushort)(mv * 64 / 5));
            Set16(0x04, (ushort)(pct << 8));
            if(pct < AlertThreshold && old >= AlertThreshold) { mem[0x1A] |= 0x10; mem[0x0D] |= 0x20; }
            UpdatePin();
        }

        protected override void WriteByte(byte reg, int index, byte v)
        {
            if(reg > 0x05 && reg != 0x08 && reg != 0x09) { mem[reg] = v; }
            UpdatePin();
        }

        protected override byte ReadByte(byte reg, int index) => mem[reg];

        private void Set16(int reg, ushort v) { mem[reg] = (byte)(v >> 8); mem[reg + 1] = (byte)v; }
        private void UpdatePin() { Alert.Set(!AlertActive); }

        private readonly byte[] mem = new byte[256];
    }

    // ------------------------------------------------------------------ BQ25180
    public class BQ25180 : RegisterI2C
    {
        public BQ25180(IMachine machine)
        {
            Int = new GPIO();
            pulse = new LimitTimer(machine.ClockSource, 1000, this, "int", limit: 1, direction: Direction.Ascending,
                                   enabled: false, workMode: WorkMode.OneShot, eventEnabled: true);
            pulse.LimitReached += () => Int.Set(true);
            Reset();
        }

        public GPIO Int { get; } // /INT, high when idle

        public override void Reset()
        {
            base.Reset();
            Array.Copy(Defaults, regs, Defaults.Length);
            pulse.Enabled = false;
            Int.Set(true);
        }

        public int ChargeCurrentMa
        {
            get { var code = regs[0x04] & 0x7F; return code <= 31 ? code + 5 : 40 + (code - 31) * 10; }
        }
        public int RegulationMv => 3500 + (regs[0x03] & 0x7F) * 10;
        public bool WatchdogDisabled => (regs[0x07] & 0x03) == 0x03;

        // state: 0 = not charging, 1 = CC, 2 = CV, 3 = done
        public void SetCharger(bool vin, int state)
        {
            var stat0 = (byte)(((state & 3) << 5) | (vin ? 1 : 0));
            if(stat0 == regs[0]) { return; }
            regs[0] = stat0;
            regs[2] |= 0x08;
            if((regs[0x06] & 0x04) == 0)
            {
                Int.Set(false);
                pulse.Enabled = false; pulse.Value = 0; pulse.Enabled = true;
            }
        }

        protected override void WriteByte(byte reg, int index, byte v)
        {
            if(reg < Defaults.Length && reg > 0x02 && reg != 0x0C) { regs[reg] = v; }
        }

        protected override byte ReadByte(byte reg, int index)
        {
            if(reg >= Defaults.Length) { return 0; }
            var v = regs[reg];
            if(reg == 0x02) { regs[0x02] = 0; }
            return v;
        }

        private static readonly byte[] Defaults = { 0x00, 0x00, 0x00, 0x46, 0x05, 0x2C, 0x56, 0x84, 0x4D, 0x11, 0x40, 0x00, 0xC0 };
        private readonly byte[] regs = new byte[13];
        private readonly LimitTimer pulse;
    }

    // ------------------------------------------------------------------ DRV2605
    // GPIO input 0 = EN. The I2C interface only answers while EN is high
    // (here: transfers while EN is low are counted as errors).
    public class DRV2605 : RegisterI2C, IGPIOReceiver
    {
        public DRV2605(IMachine machine)
        {
            timer = new LimitTimer(machine.ClockSource, 1000, this, "go", limit: 1, direction: Direction.Ascending,
                                   enabled: false, workMode: WorkMode.OneShot, eventEnabled: true);
            timer.LimitReached += () => regs[0x0C] = 0;
            Reset();
        }

        public int Plays { get; private set; }
        public int LastEffect { get; private set; }
        public int Errors { get; private set; }
        public bool Standby => (regs[0x01] & 0x40) != 0;
        public bool ErmOpenLoop => (regs[0x1A] & 0x80) == 0 && (regs[0x1D] & 0x20) != 0 && regs[0x03] == 1;

        public override void Reset()
        {
            base.Reset();
            Defaults();
            Plays = 0; LastEffect = 0; Errors = 0;
            timer.Enabled = false;
        }

        public void OnGPIO(int number, bool value) { if(number == 0) { enabled = value; } }

        protected override void WriteByte(byte reg, int index, byte v)
        {
            if(!enabled) { Errors++; return; }
            if(reg == 0x00 || reg >= regs.Length) { return; }
            if(reg == 0x01 && (v & 0x80) != 0) { Defaults(); return; } // reset completes instantly here
            regs[reg] = v;
            if(reg == 0x0C && (v & 1) != 0)
            {
                if(Standby) { Errors++; regs[0x0C] = 0; return; }
                Plays++;
                LastEffect = regs[0x04];
                timer.Enabled = false; timer.Value = 0;
                timer.Limit = (ulong)(LastEffect == 14 ? 500 : LastEffect == 10 ? 150 : 60);
                timer.Enabled = true;
            }
        }

        protected override byte ReadByte(byte reg, int index)
        {
            if(!enabled) { Errors++; return 0; }
            return reg < regs.Length ? regs[reg] : (byte)0;
        }

        private void Defaults()
        {
            Array.Clear(regs, 0, regs.Length);
            regs[0x00] = 0x60; regs[0x01] = 0x40; regs[0x03] = 0x01; regs[0x04] = 0x01;
            regs[0x16] = 0x3E; regs[0x17] = 0x8C; regs[0x1A] = 0x36; regs[0x1D] = 0xA0;
        }

        private readonly byte[] regs = new byte[0x23];
        private readonly LimitTimer timer;
        private bool enabled;
    }
}
