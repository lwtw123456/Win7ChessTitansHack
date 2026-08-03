import 'dart:ffi';
import 'dart:io';
import 'package:ffi/ffi.dart';
import 'package:win32/win32.dart';
import 'package:path/path.dart' as p;

typedef _PapcFuncNative = Void Function(UintPtr parameter);
typedef QNative = Uint32 Function(Pointer<NativeFunction<Void Function(UintPtr)>>, Pointer<Void>, UintPtr);
typedef ResumeThreadN = Uint32 Function(Pointer<Void>);


class InjectionService {
  InjectionService();

  Future<void> runInjection() async {
    final exeDir = p.dirname(Platform.resolvedExecutable);
    final targetPath = p.join(exeDir, 'chess.exe');
    final dllPath = p.join(exeDir, 'chess_hack.dll');
  
    if (!File(targetPath).existsSync() || !File(dllPath).existsSync()) {
      throw Exception('找不到目标文件，请确认同目录存在 chess.exe 和 chess_hack.dll');
    }
  
    final si = calloc<STARTUPINFO>()
      ..ref.cb = sizeOf<STARTUPINFO>();
  
    final pi = calloc<PROCESS_INFORMATION>();
  
    final applicationName = targetPath.toPcwstr();
  
    final result = CreateProcess(
	  applicationName,
	  null,
	  null,
	  null,
	  false,
	  CREATE_SUSPENDED,
	  null,
	  null,
	  si,
	  pi,
    );

    if (!result.value) {
	  throw WindowsException(
	    result.error.toHRESULT(),
	    message: 'CreateProcess 失败，错误码: ${result.error.code}',
	  );
    }
  
	final hProcess = pi.ref.hProcess;
	final hThread = pi.ref.hThread;

	final dllPathPtr = dllPath.toPcwstr();

	final dllPathBytes =
		dllPathPtr.byteLength + sizeOf<Uint16>();

	final Win32Result(value: pRemote, :error) = VirtualAllocEx(
	  hProcess,
	  null,
	  dllPathBytes,

	  VIRTUAL_ALLOCATION_TYPE(
		MEM_COMMIT | MEM_RESERVE,
	  ),

	  PAGE_READWRITE,
	);

	if (pRemote.isNull) {
	  free(dllPathPtr);
	  _cleanup(hProcess, hThread, si, pi);

	  throw WindowsException(
		error.toHRESULT(),
		message: 'VirtualAllocEx 失败，Win32 错误码: ${error.toInt()}',
	  );
	}

	final writeResult = WriteProcessMemory(
	  hProcess,
	  pRemote,
	  dllPathPtr.cast<NativeType>(),
	  dllPathBytes,
	  null,
	);

	free(dllPathPtr);

	if (!writeResult.value) {
	  final error = writeResult.error;
	  _cleanup(hProcess, hThread, si, pi);
	  throw WindowsException(
		error.toHRESULT(),
		message: 'WriteProcessMemory 失败，Win32 错误码: $error',
	  );
	}
  
	final (hKernel32, pLoadLibraryW) = using((arena) {
	  final hKernel32 =
		  GetModuleHandle(arena.pcwstr('kernel32.dll')).value;

	  if (hKernel32.isNull) {
		_cleanup(hProcess, hThread, si, pi);
		throw Exception('无法获取 kernel32.dll 句柄');
	  }

	  final pLoadLibraryW =
		  GetProcAddress(hKernel32, arena.pcstr('LoadLibraryW')).value;

	  if (pLoadLibraryW.isNull) {
		_cleanup(hProcess, hThread, si, pi);
		throw Exception('无法获取 LoadLibraryW 地址');
	  }
	  
	  return (hKernel32, pLoadLibraryW);
	});
  
	final kernel32 = DynamicLibrary.open('kernel32.dll');

	final queueUserApc = kernel32.lookupFunction<
		QNative,
		int Function(
		  Pointer<NativeFunction<Void Function(UintPtr)>>,
		  Pointer<Void>,
		  int,
		)>('QueueUserAPC');

	final apcQueued = queueUserApc(
	  Pointer<NativeFunction<_PapcFuncNative>>.fromAddress(
		pLoadLibraryW.address,
	  ),
	  hThread.cast<Void>(),
	  pRemote.address,
	);

	if (apcQueued == 0) {
	  _cleanup(hProcess, hThread, si, pi);
	  throw Exception('QueueUserAPC 失败');
	}

	final resumeThread = kernel32.lookupFunction<
		ResumeThreadN,
		int Function(Pointer<Void>)
	  >('ResumeThread');

	final resumeResult = resumeThread(
	  hThread.cast<Void>(),
	);

	if (resumeResult == 0xFFFFFFFF) {
	  final error = GetLastError();

	  throw WindowsException(
		error.toHRESULT(),
		message: 'ResumeThread 失败，错误码: ${error.code}',
	  );
	}
	  
    await waitForDll(
      hProcess,
      'chess_hack.dll',
    );
	
    _cleanup(hProcess, hThread, si, pi);
  }
  
    bool processHasDll(HANDLE process, String dllName) {
      return using((arena) {
        const capacity = 1024;
    
        final modules = arena<Pointer>(capacity);
        final bytesNeeded = arena<DWORD>();
    
        final result = EnumProcessModules(
          process,
          modules,
          capacity * sizeOf<Pointer>(),
          bytesNeeded,
        );
    
        if (!result.value) return false;
    
        final rawCount = bytesNeeded.value ~/ sizeOf<Pointer>();
        final count = rawCount > capacity ? capacity : rawCount;
        final target = dllName.toLowerCase();
    
        final pathBuffer = arena.pwstrBuffer(MAX_PATH);
    
        for (var i = 0; i < count; i++) {
          final module = HMODULE((modules + i).value);
    
          final result = GetModuleFileNameEx(
            process,
            module,
            pathBuffer,
            MAX_PATH,
          );
    
          if (result.value == 0) continue;
    
          final moduleName = pathBuffer
              .toDartString()
              .split('\\')
              .last
              .toLowerCase();
    
          if (moduleName == target) return true;
        }
    
        return false;
      });
    }
    
    Future<bool> waitForDll(
      HANDLE process,
      String dllName, {
      Duration timeout = const Duration(seconds: 5),
      Duration interval = const Duration(milliseconds: 50),
    }) async {
      final deadline = DateTime.now().add(timeout);
    
      do {
        if (processHasDll(process, dllName)) {
          return true;
        }
    
        await Future.delayed(interval);
      } while (DateTime.now().isBefore(deadline));
    
      return false;
    }
    
	void _cleanup(
	  HANDLE hProcess,
	  HANDLE hThread,
	  Pointer<STARTUPINFO> si,
	  Pointer<PROCESS_INFORMATION> pi,
	) {
	  if (hThread.isValid) {
		CloseHandle(hThread);
	  }

	  if (hProcess.isValid) {
		CloseHandle(hProcess);
	  }

	  calloc.free(si);
	  calloc.free(pi);
	}
  }