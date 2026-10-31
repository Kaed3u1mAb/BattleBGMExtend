"""Optional local-game FMOD audio test; no injection, speaker output or assets copied.

python src/tests/probe_score_audio.py --game-data '.../LimbusCompany_Data'
Uses a fresh NOSOUND mixer per case and meters real decoded PCM output.
"""
import argparse
import ctypes as c
import json
from pathlib import Path
import time


class Meter(c.Structure):
    _fields_ = [('samples', c.c_int), ('peak', c.c_float * 32),
                ('rms', c.c_float * 32), ('channels', c.c_short)]


def probe(game_data, track, ryoshu):
    lib = c.CDLL(str(game_data / 'Plugins/x86_64/fmodstudio.dll'))
    ptr = c.c_void_p

    def fn(name, args):
        method = getattr(lib, name)
        method.argtypes, method.restype = args, c.c_int
        return method

    def check(result):
        if result != 0:
            raise RuntimeError(f'FMOD result: {result}')

    system = ptr()
    check(fn('FMOD_Studio_System_Create', [c.POINTER(ptr), c.c_uint])(
        c.byref(system), 0x00020226))
    try:
        core = ptr()
        check(fn('FMOD_Studio_System_GetCoreSystem', [ptr, c.POINTER(ptr)])(
            system, c.byref(core)))
        check(fn('FMOD5_System_SetOutput', [ptr, c.c_int])(core, 2))  # NOSOUND
        check(fn('FMOD_Studio_System_Initialize', [ptr, c.c_int, c.c_uint, c.c_uint, ptr])(
            system, 128, 0, 0, None))
        for name in ('Master.bank', 'Master.strings.bank',
                     'BGM_Default_S7_3.assets.bank', 'BGM_Default_S7_3.bank'):
            bank = ptr()
            path = game_data / 'StreamingAssets/Assets/Sound/FMODBuilds/Desktop' / name
            check(fn('FMOD_Studio_System_LoadBankFile',
                     [ptr, c.c_char_p, c.c_uint, c.POINTER(ptr)])(
                         system, str(path).encode('utf-8'), 0, c.byref(bank)))
        group, dsp = ptr(), ptr()
        check(fn('FMOD5_System_GetMasterChannelGroup', [ptr, c.POINTER(ptr)])(
            core, c.byref(group)))
        check(fn('FMOD5_ChannelGroup_GetDSP', [ptr, c.c_int, c.POINTER(ptr)])(
            group, 0, c.byref(dsp)))
        check(fn('FMOD5_DSP_SetMeteringEnabled', [ptr, c.c_int, c.c_int])(dsp, 0, 1))
        desc, event = ptr(), ptr()
        check(fn('FMOD_Studio_System_GetEvent', [ptr, c.c_char_p, c.POINTER(ptr)])(
            system, f'event:/BGM/Battle/{track}'.encode(), c.byref(desc)))
        check(fn('FMOD_Studio_EventDescription_CreateInstance', [ptr, c.POINTER(ptr)])(
            desc, c.byref(event)))
        set_param = fn('FMOD_Studio_EventInstance_SetParameterByName',
                       [ptr, c.c_char_p, c.c_float, c.c_int])
        # StartAddBGM sets Playing=1. Reproduce that on both silent/fixed cases.
        check(set_param(event, b'Playing', 1., 1))
        if ryoshu is not None:
            check(set_param(event, b'Ryoshu', float(ryoshu), 1))
        check(fn('FMOD_Studio_EventInstance_Start', [ptr])(event))
        peak = 0.
        for _ in range(35):
            check(fn('FMOD_Studio_System_Update', [ptr])(system))
            time.sleep(.1)
            meter = Meter()
            check(fn('FMOD5_DSP_GetMeteringInfo', [ptr, ptr, c.POINTER(Meter)])(
                dsp, None, c.byref(meter)))
            peak = max(peak, max(meter.peak))
        state = c.c_int()
        check(fn('FMOD_Studio_EventInstance_GetPlaybackState', [ptr, c.POINTER(c.c_int)])(
            event, c.byref(state)))
        return dict(track=track, Ryoshu=ryoshu, playback_state=state.value, peak=peak)
    finally:
        check(fn('FMOD_Studio_System_Release', [ptr])(system))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--game-data', required=True, type=Path)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    results = [probe(args.game_data, track, value) for track, value in (
        ('Battle_Cp9_Boss_10', None), ('Battle_Cp9_Boss_10', 1),
        ('Battle_Cp9_Boss_1_2', None))]
    text = json.dumps(results, indent=2)
    print(text)
    if args.output:
        args.output.write_text(text + '\n', encoding='utf-8')
    assert results[0]['playback_state'] == 0 and results[0]['peak'] < 1e-7, results[0]
    assert all(row['peak'] > .001 for row in results[1:]), results
