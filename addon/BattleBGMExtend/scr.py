"""BattleBGMExtend entry point for FaustLauncher (Windows x64)."""
import ctypes
from ctypes import wintypes as wt
import json
import re
import mmap
import os
import struct
import threading
import time

MANAGER = ADDON_ARG['AddonManager']
NAME = ADDON_ARG['AddonName']
HERE = os.path.dirname(os.path.abspath(__file__))
MAGIC, VERSION, HEADER_SIZE, MAX_RULES = 0x31454742, 2, 512, 32
API_NAMES = ('il2cpp_domain_get', 'il2cpp_domain_get_assemblies', 'il2cpp_assembly_get_image', 'il2cpp_class_from_name', 'il2cpp_class_get_method_from_name', 'il2cpp_class_get_field_from_name', 'il2cpp_field_get_offset', 'il2cpp_method_get_param_count', 'il2cpp_method_get_return_type', 'il2cpp_method_get_param', 'il2cpp_type_get_name', 'il2cpp_free', 'il2cpp_method_get_flags', 'il2cpp_field_get_type', 'il2cpp_type_get_type', 'il2cpp_resolve_icall', 'il2cpp_class_get_parent', 'il2cpp_field_static_get_value', 'il2cpp_runtime_invoke', 'il2cpp_object_unbox', 'il2cpp_object_get_class', 'il2cpp_object_get_virtual_method', 'il2cpp_gchandle_new', 'il2cpp_gchandle_free', 'il2cpp_gchandle_get_target', 'il2cpp_class_is_assignable_from', 'il2cpp_thread_attach', 'il2cpp_thread_detach', 'il2cpp_field_get_value', 'il2cpp_field_set_value', 'il2cpp_string_new', 'il2cpp_object_new', 'il2cpp_class_get_methods', 'il2cpp_method_get_name')
RULE_OFFSET = HEADER_SIZE + len(API_NAMES) * 64
MAP_SIZE = RULE_OFFSET + MAX_RULES * 384
ERRORS = {0: "正常", 1: "IL2CPP 导出映射不可用", 2: "等待游戏元数据", 3: "方法签名不匹配", 4: "字段或规则标识不匹配", 5: "MinHook 初始化失败", 6: "创建钩子失败", 7: "启用钩子失败", 20: "游戏方法调用异常", 21: "目标 BGM 未能创建/播放"}
STOP = threading.Event()
REQUEST = threading.Event()
RETRY = threading.Event()
LOCK = threading.RLock()
_thread = None
_session = None
_paused = False

def log(message):
    print(f'[BattleBGMExtend] {message}')

def settings():
    return (MANAGER.get_addon_info(NAME) or {}).get('settings') or {}

def load_rules(path=None):
    with open(path or os.path.join(HERE, 'rules.json'), encoding='utf-8-sig') as stream:
        document = json.load(stream)
    if not isinstance(document, dict) or document.get('schema_version') != 1:
        raise ValueError('rules.json 的 schema_version 必须为 1')
    rows = document.get('rules')
    if not isinstance(rows, list) or len(rows) > MAX_RULES:
        raise ValueError(f'rules 必须为数组，最多 {MAX_RULES} 条')
    result, ids = bytearray(), set()
    def ascii_field(value, size, label):
        if not isinstance(value, str) or not value or not re.fullmatch(r'[A-Za-z0-9_:/.-]+', value):
            raise ValueError(f'{label} 必须为非空 ASCII 标识')
        raw = value.encode('ascii')
        if len(raw) >= size:
            raise ValueError(f'{label} 过长（上限 {size-1} 字节）')
        return raw.ljust(size, b'\0')
    for row in rows:
        if not isinstance(row, dict):
            raise ValueError('规则必须为对象')
        if type(row.get('enabled', True)) is not bool:
            raise ValueError('enabled 必须为布尔值')
        if not row.get('enabled', True):
            continue
        key = row.get('id')
        rule_id = ascii_field(key, 48, 'id')
        if key in ids:
            raise ValueError(f'重复规则 id: {key}')
        ids.add(key)
        condition = row.get('condition')
        kinds = {'has_active_buff': 0, 'round_start_buff_stack_equals': 1,
                 'round_start_without_buff': 2, 'round_end_buff_transition': 3}
        if not isinstance(condition, dict) or condition.get('type') not in kinds:
            raise ValueError('不支持的 condition.type')
        kind = kinds[condition['type']]
        threshold = condition.get('stack', 0)
        if type(threshold) is not int or not 0 <= threshold <= 2147483647:
            raise ValueError('stack 必须为非负 int32 整数')
        if kind == 1 and 'stack' not in condition:
            raise ValueError('层数条件必须指定 stack')
        from_keyword = (ascii_field(condition.get('from_keyword'), 64, 'from_keyword')
                        if kind == 3 else b'\0' * 64)
        personality, priority = row.get('personality_id'), row.get('priority')
        if type(personality) is not int or not 1 <= personality <= 2147483647:
            raise ValueError('personality_id 必须为正整数')
        if type(priority) is not int or not -2147483648 <= priority <= 2147483647:
            raise ValueError('priority 必须为 int32 整数（数值越大优先级越高）')
        keyword = ascii_field(condition.get('keyword'), 64, 'keyword')
        bgm = row.get('bgm')
        ascii_field(bgm, 128, 'bgm')
        if not bgm.startswith('event:/'):
            bgm = 'event:/BGM/Battle/' + bgm
        bgm = ascii_field(bgm, 128, 'bgm path')
        bank = row.get('bank', '')
        bank = ascii_field(bank, 64, 'bank') if bank else b'\0' * 64
        if bank.strip(b'\0') and not re.fullmatch(rb'[A-Za-z0-9_-]+', bank.rstrip(b'\0')):
            raise ValueError('bank 只能包含字母、数字、下划线和连字符')
        if not bgm.startswith(b'event:/BGM/Battle/') or not bgm[18:].split(b'\0')[0]:
            raise ValueError('bgm 必须为战斗 BGM 路径')
        result.extend(struct.pack('<ii64s128s48s64sii64s', personality, priority, keyword, bgm, rule_id, bank, kind, threshold, from_keyword))
    return bytes(result)

def tick():
    return int(K.GetTickCount())

K = ctypes.WinDLL('kernel32', use_last_error=True)
P = ctypes.c_void_p
DWORD, BOOL, HANDLE = wt.DWORD, wt.BOOL, P
INVALID = ctypes.c_void_p(-1).value

class ProcessEntry(ctypes.Structure):
    _fields_ = [('dwSize', DWORD), ('cntUsage', DWORD), ('th32ProcessID', DWORD),
                ('th32DefaultHeapID', ctypes.c_size_t), ('th32ModuleID', DWORD),
                ('cntThreads', DWORD), ('th32ParentProcessID', DWORD),
                ('pcPriClassBase', wt.LONG), ('dwFlags', DWORD),
                ('szExeFile', wt.WCHAR * 260)]

class ModuleEntry(ctypes.Structure):
    _fields_ = [('dwSize', DWORD), ('th32ModuleID', DWORD), ('th32ProcessID', DWORD),
                ('GlblcntUsage', DWORD), ('ProccntUsage', DWORD),
                ('modBaseAddr', P), ('modBaseSize', DWORD), ('hModule', HANDLE),
                ('szModule', wt.WCHAR * 256), ('szExePath', wt.WCHAR * 260)]

class MemoryInfo(ctypes.Structure):
    _fields_ = [('BaseAddress', P), ('AllocationBase', P),
                ('AllocationProtect', DWORD), ('PartitionId', wt.WORD),
                ('RegionSize', ctypes.c_size_t), ('State', DWORD),
                ('Protect', DWORD), ('Type', DWORD)]

def bind(name, result, *args):
    fn = getattr(K, name)
    fn.restype, fn.argtypes = result, list(args)
    return fn

bind('GetTickCount', DWORD)
bind('CreateToolhelp32Snapshot', HANDLE, DWORD, DWORD)
bind('CloseHandle', BOOL, HANDLE)
bind('Process32FirstW', BOOL, HANDLE, ctypes.POINTER(ProcessEntry))
bind('Process32NextW', BOOL, HANDLE, ctypes.POINTER(ProcessEntry))
bind('Module32FirstW', BOOL, HANDLE, ctypes.POINTER(ModuleEntry))
bind('Module32NextW', BOOL, HANDLE, ctypes.POINTER(ModuleEntry))
bind('OpenProcess', HANDLE, DWORD, BOOL, DWORD)
bind('VirtualAllocEx', P, HANDLE, P, ctypes.c_size_t, DWORD, DWORD)
bind('VirtualFreeEx', BOOL, HANDLE, P, ctypes.c_size_t, DWORD)
bind('WriteProcessMemory', BOOL, HANDLE, P, P, ctypes.c_size_t,
     ctypes.POINTER(ctypes.c_size_t))
bind('GetModuleHandleW', HANDLE, wt.LPCWSTR)
bind('GetProcAddress', P, HANDLE, ctypes.c_char_p)
bind('VirtualQuery', ctypes.c_size_t, P, ctypes.POINTER(MemoryInfo), ctypes.c_size_t)
bind('GetModuleFileNameW', DWORD, HANDLE, wt.LPWSTR, DWORD)
bind('CreateRemoteThread', HANDLE, HANDLE, P, ctypes.c_size_t, P, P, DWORD, P)
bind('WaitForSingleObject', DWORD, HANDLE, DWORD)
bind('GetExitCodeThread', BOOL, HANDLE, ctypes.POINTER(DWORD))

def winerror(operation):
    return OSError(f'{operation}: {ctypes.WinError(ctypes.get_last_error())}')

def snapshot(flags, pid=0):
    for _ in range(4):
        handle = K.CreateToolhelp32Snapshot(flags, pid)
        if handle and handle != INVALID:
            return handle
        if ctypes.get_last_error() != 24:
            break  # ERROR_BAD_LENGTH: transient module-list update
    raise winerror('进程快照失败')

def game_pid():
    handle = snapshot(2)
    try:
        item = ProcessEntry()
        item.dwSize = ctypes.sizeof(item)
        more = K.Process32FirstW(handle, ctypes.byref(item))
        while more:
            if item.szExeFile.lower() == 'limbuscompany.exe':
                return int(item.th32ProcessID)
            more = K.Process32NextW(handle, ctypes.byref(item))
        return None
    finally:
        K.CloseHandle(handle)

def modules(pid):
    handle = snapshot(0x18, pid)
    result = {}
    try:
        item = ModuleEntry()
        item.dwSize = ctypes.sizeof(item)
        more = K.Module32FirstW(handle, ctypes.byref(item))
        while more:
            result[item.szModule.lower()] = (int(item.modBaseAddr), item.szExePath)
            more = K.Module32NextW(handle, ctypes.byref(item))
        return result
    finally:
        K.CloseHandle(handle)

def remote_loader(module_list):
    local = K.GetProcAddress(K.GetModuleHandleW('kernel32.dll'), b'LoadLibraryW')
    info = MemoryInfo()
    if not local or not K.VirtualQuery(local, ctypes.byref(info), ctypes.sizeof(info)):
        raise winerror('定位 LoadLibraryW 失败')
    path = ctypes.create_unicode_buffer(32768)
    if not K.GetModuleFileNameW(info.AllocationBase, path, len(path)):
        raise winerror('获取加载函数所属模块失败')
    remote = module_list.get(os.path.basename(path.value).lower())
    if not remote:
        raise RuntimeError('游戏尚未加载系统 DLL')
    return remote[0] + int(local) - int(info.AllocationBase)

def release_load(process, remote, thread):
    # Keep the path allocation alive until LoadLibraryW finishes, even when
    # plugin shutdown cancels our wait. This thread only releases resources.
    try:
        while K.WaitForSingleObject(thread, 1000) == 0x102:
            pass
        if K.WaitForSingleObject(thread, 0) == 0:
            K.VirtualFreeEx(process, remote, 0, 0x8000)
    finally:
        K.CloseHandle(thread)
        K.CloseHandle(process)

def inject(pid, module_list):
    if STOP.is_set():
        return
    dll = os.path.join(HERE, 'battlebgmextend.dll')
    if not os.path.isfile(dll):
        raise FileNotFoundError(f'缺少 {dll}')
    if 'battlebgmextend.dll' in module_list:
        return
    loader = remote_loader(module_list)
    process = K.OpenProcess(0x0002 | 0x0400 | 0x0008 | 0x0020 | 0x0010, False, pid)
    if not process:
        raise winerror('打开游戏进程失败')
    remote, thread = None, None
    deferred = False
    try:
        data = dll.encode('utf-16-le') + b'\0\0'
        remote = K.VirtualAllocEx(process, None, len(data), 0x3000, 4)
        if not remote:
            raise winerror('分配 DLL 路径缓冲区失败')
        buffer = ctypes.create_string_buffer(data)
        written = ctypes.c_size_t()
        if not K.WriteProcessMemory(process, remote, buffer, len(data), ctypes.byref(written)) or written.value != len(data):
            raise winerror('写入 DLL 路径失败')
        if STOP.is_set():
            return
        thread = K.CreateRemoteThread(process, None, 0, loader, remote, 0, None)
        if not thread:
            raise winerror('加载 DLL 失败')
        deadline = time.monotonic() + 20
        wait_status = K.WaitForSingleObject(thread, 0)
        while wait_status == 0x102:
            if STOP.wait(0.1) or time.monotonic() >= deadline:
                threading.Thread(target=release_load, args=(process, remote, thread),
                                 daemon=True, name='BattleBGMExtend-loader-cleanup').start()
                deferred = True
                raise RuntimeError('DLL 加载等待已取消或超时；等待系统线程结束后再重试')
            wait_status = K.WaitForSingleObject(thread, 0)
        if wait_status != 0:
            # Do not release a buffer while its thread completion is unknown.
            threading.Thread(target=release_load, args=(process, remote, thread),
                             daemon=True, name='BattleBGMExtend-loader-cleanup').start()
            deferred = True
            raise RuntimeError(f'无法确认 DLL 加载线程已完成（WaitForSingleObject={wait_status}）')
        # DWORD thread exit codes truncate HMODULE on x64. Verify the actual
        # module list instead of interpreting the low 32 bits as a pointer.
        if 'battlebgmextend.dll' not in modules(pid):
            raise RuntimeError('系统未加载 battlebgmextend.dll，请检查 DLL 架构和依赖')
    finally:
        if not deferred:
            if remote:
                K.VirtualFreeEx(process, remote, 0, 0x8000)
            if thread:
                K.CloseHandle(thread)
            K.CloseHandle(process)

class Session:
    def __init__(self, pid, game_dll):
        self.pid, self.attempted = pid, False
        self.memory = mmap.mmap(-1, MAP_SIZE,
            tagname=f'Local\\FaustLauncher_BattleBGMExtend_v2_{pid}', access=mmap.ACCESS_WRITE)
        if struct.unpack_from('<III', self.memory) != (MAGIC, VERSION, HEADER_SIZE):
            self.memory[:] = b'\0' * MAP_SIZE
            struct.pack_into('<III', self.memory, 0, MAGIC, VERSION, HEADER_SIZE)
        self.write(12, 0)
        try:
            payload = load_rules()
        except Exception:
            self.memory.close()
            raise
        from functions.base import il2cpp_exports
        result = il2cpp_exports.resolve(
            game_dir=os.path.dirname(game_dll),
            names_path=os.path.join(HERE, 'il2cpp-api-names.txt'))
        if not result.get('ok'):
            self.memory.close()
            raise RuntimeError(f"导出名解析失败：{result.get('reason', '未知原因')}")
        mapping = result.get('mapping') or {}
        for index, name in enumerate(API_NAMES):
            alias = str(mapping.get(name) or name).encode('ascii')
            if len(alias) >= 64:
                self.memory.close()
                raise RuntimeError(f'导出名过长：{name}')
            self.memory[HEADER_SIZE + index * 64:HEADER_SIZE + (index + 1) * 64] = alias.ljust(64, b'\0')

        # Publish while disabled; native reads a coherent snapshot before activation.
        previous = struct.unpack_from('<I', self.memory, 68)[0]
        self.write(68, ((previous + 1) | 1) & 0xffffffff)
        self.memory[RULE_OFFSET:] = payload.ljust(MAX_RULES * 384, b'\0')
        self.write(64, len(payload) // 384)
        self.write(68, (struct.unpack_from('<I', self.memory, 68)[0] + 1) & 0xffffffff or 2)

    def write(self, offset, value):
        struct.pack_into('<I', self.memory, offset, value & 0xffffffff)

    def update(self, enabled):
        self.write(16, tick())
        self.write(12, int(bool(enabled)))

    def status(self):
        words = struct.unpack_from('<20I', self.memory)
        return {'hooked': bool(words[5]), 'error': words[6], 'updates': words[8],
                'changes': words[9], 'blocked': words[10], 'locked': bool(words[11]),
                'priority': ctypes.c_int32(words[12]).value,
                'rule': ctypes.c_int32(words[13]).value,
                'phase': ctypes.c_int32(words[14]).value, 'sequence': words[15],
                'message': self.memory[80:336].split(b'\0')[0].decode('utf-8', 'replace'),
                'bgm': self.memory[336:464].split(b'\0')[0].decode('ascii', 'replace')}

    def close(self):
        self.write(12, 0)
        self.memory.close()

def worker():
    global _session
    last_state = None
    try:
        while not STOP.is_set():
            try:
                active = bool(settings().get('enable', True)) and not _paused
                wanted = active and (bool(settings().get('自动注入', True)) or REQUEST.is_set())
                pid = game_pid()
                with LOCK:
                    if _session and _session.pid != pid:
                        _session.close()
                        _session = None
                    if _session:
                        _session.update(active)
                if pid and wanted:
                    module_list = modules(pid)
                    assembly = module_list.get('gameassembly.dll')
                    if assembly:
                        with LOCK:
                            if _session is None:
                                _session = Session(pid, assembly[1])
                            _session.update(active and not STOP.is_set())
                            if RETRY.is_set():
                                _session.attempted = False
                                RETRY.clear()
                            should_load = not _session.attempted and not STOP.is_set()
                            if should_load:
                                _session.attempted = True
                        if should_load:
                            inject(pid, module_list)
                            log(f'已连接游戏 PID={pid}，等待 BGM 规则钩子就绪')
                with LOCK:
                    state = _session.status() if _session else None
                key = (pid, active, state and state['hooked'], state and state['error'], state and state['sequence'])
                if key != last_state:
                    if state:
                        log(f"{'生效' if state['hooked'] and active else '等待或已暂停'}；"
                            f"{ERRORS.get(state['error'], state['error'])}；{state['message']}")
                    else:
                        log('等待游戏启动' if wanted else '等待手动连接')
                    last_state = key
            except Exception as exc:
                key = ('error', str(exc))
                if key != last_state:
                    log(str(exc))
                    last_state = key
            STOP.wait(0.5)
    finally:
        with LOCK:
            if _session:
                _session.close()
                _session = None

def connect(icon=None, item=None):
    global _paused
    _paused = False
    REQUEST.set()
    RETRY.set()

def pause(icon=None, item=None):
    global _paused
    _paused = True
    with LOCK:
        if _session:
            _session.update(False)
    log('已暂停：解除音乐锁定，恢复游戏音乐控制')

def show_status(icon=None, item=None):
    with LOCK:
        state = _session.status() if _session else None
    if state is None:
        log('尚未连接游戏')
        return
    text = (f"{'锁定' if state['locked'] else '未锁定'}；BGM={state['bgm'] or '游戏原音乐'}；"
            f"优先级={state['priority']}；切换={state['changes']}；拦截={state['blocked']}；"
            f"{state['message']}")
    log(text)
    if icon is not None and hasattr(icon, 'notify'):
        try:
            icon.notify(text, 'BattleBGMExtend')
        except Exception:
            pass

def on_unload():
    STOP.set()
    with LOCK:
        if _session:
            _session.update(False)
    if _thread and _thread is not threading.current_thread():
        _thread.join(timeout=3)

if ctypes.sizeof(P) != 8:
    raise RuntimeError('BattleBGMExtend 需要 64 位启动器')

# Tests load the entry in a temporary fake AddonManager without any game access.
if os.environ.get('BATTLE_BGM_EXTEND_TEST') != '1':
    try:
        import pystray
        submenu = pystray.Menu(
            pystray.MenuItem('连接 / 恢复', connect),
            pystray.MenuItem('暂停', pause),
            pystray.MenuItem('查看状态', show_status),
        )
        menu = pystray.MenuItem('BattleBGMExtend', action=submenu)
        register = getattr(MANAGER, 'register_tray_item', None)
        if callable(register):
            register(menu)
        else:
            MANAGER.menu_items.append(menu)
    except ImportError:
        log('pystray 不可用，跳过托盘菜单')
    _thread = threading.Thread(target=worker, daemon=True, name='BattleBGMExtend-worker')
    _thread.start()
    log('已加载：按人格 / Buff 规则切换战斗 BGM，并按优先级锁定')
