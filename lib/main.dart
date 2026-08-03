import 'dart:async';
import 'dart:convert';
import 'dart:io';

import 'package:flutter/material.dart';

import 'injection_service.dart';

void main() => runApp(const MyApp());

class MyApp extends StatelessWidget {
  const MyApp({super.key});

  @override
  Widget build(BuildContext context) {
    const primary = Color(0xff5b67d8);

    return MaterialApp(
      debugShowCheckedModeBanner: false,
      title: '国际象棋两项修改器',
      theme: ThemeData(
        useMaterial3: true,
        colorScheme: ColorScheme.fromSeed(
          seedColor: primary,
          brightness: Brightness.light,
        ),
        scaffoldBackgroundColor: const Color(0xfff4f5f8),
        fontFamily: Platform.isWindows ? 'Microsoft YaHei UI' : null,
        visualDensity: VisualDensity.compact,
        textTheme: const TextTheme(
          titleLarge: TextStyle(
            fontSize: 18,
            fontWeight: FontWeight.w600,
            letterSpacing: -0.2,
          ),
          titleMedium: TextStyle(
            fontSize: 14,
            fontWeight: FontWeight.w600,
          ),
          bodyMedium: TextStyle(fontSize: 13, height: 1.35),
          bodySmall: TextStyle(fontSize: 11.5, height: 1.3),
          labelLarge: TextStyle(fontSize: 13, fontWeight: FontWeight.w600),
        ),
        filledButtonTheme: FilledButtonThemeData(
          style: FilledButton.styleFrom(
            minimumSize: const Size(0, 38),
            padding: const EdgeInsets.symmetric(horizontal: 14),
            shape: RoundedRectangleBorder(
              borderRadius: BorderRadius.circular(9),
            ),
          ),
        ),
        outlinedButtonTheme: OutlinedButtonThemeData(
          style: OutlinedButton.styleFrom(
            minimumSize: const Size(0, 38),
            padding: const EdgeInsets.symmetric(horizontal: 14),
            side: const BorderSide(color: Color(0xffd8dbe5)),
            shape: RoundedRectangleBorder(
              borderRadius: BorderRadius.circular(9),
            ),
          ),
        ),
        dividerTheme: const DividerThemeData(
          color: Color(0xffe7e8ed),
          thickness: 1,
          space: 1,
        ),
      ),
      home: const HomePage(),
    );
  }
}

class HomePage extends StatefulWidget {
  const HomePage({super.key});

  @override
  State<HomePage> createState() => _HomePageState();
}

class _HomePageState extends State<HomePage> {
  final logs = <String>[];
  final scroll = ScrollController();

  bool ready = false;

  bool installed = false;
  bool installBusy = false;

  bool running = false;
  bool runBusy = false;

  @override
  void initState() {
    super.initState();
    startup();
  }

  Future<void> startup() async {
    try {
      addLog('正在执行 DLL 注入...');
      final service = InjectionService();
      await service.runInjection();

      if (!mounted) return;
      setState(() => ready = true);

      addLog('注入完成');
    } catch (e) {
      addLog('注入失败：$e');
    }
  }

  Future<void> prepare() async {
    final service = InjectionService();
    await service.runInjection();
  }

  Future<String> sendCommand(String command) async {
    Socket? socket;

    try {
      socket = await Socket.connect(
        '127.0.0.1',
        27654,
        timeout: const Duration(seconds: 3),
      );

      addLog('发送：$command');

      socket.add(ascii.encode('$command\n'));
      await socket.flush();

      return await socket
          .cast<List<int>>()
          .transform(const Utf8Decoder(allowMalformed: true))
          .transform(const LineSplitter())
          .first
          .timeout(const Duration(seconds: 5));
    } finally {
      socket?.destroy();
    }
  }

  Future<void> send(
    String command, {
    required bool installGroup,
    required VoidCallback onSuccess,
  }) async {
    if (!ready) return;
    if (installGroup ? installBusy : runBusy) return;

    setBusy(installGroup, true);

    try {
      final result = await sendCommand(command);
      addLog('收到：$result');

      if (result.trimLeft().toUpperCase().startsWith('ERR')) {
        addLog('操作失败，状态未改变');
        return;
      }

      if (mounted) setState(onSuccess);
    } on TimeoutException {
      addLog('等待服务端响应超时');
    } on SocketException catch (e) {
      addLog('连接失败：${e.message}');
    } catch (e) {
      addLog('执行失败：$e');
    } finally {
      setBusy(installGroup, false);
    }
  }

  void setBusy(bool installGroup, bool value) {
    if (!mounted) return;
    setState(() => installGroup ? installBusy = value : runBusy = value);
  }

  void addLog(String text) {
    if (!mounted) return;

    setState(() {
      logs.add('${time()}  $text');
      if (logs.length > 500) logs.removeAt(0);
    });

    WidgetsBinding.instance.addPostFrameCallback((_) {
      if (!scroll.hasClients) return;

      scroll.animateTo(
        scroll.position.maxScrollExtent,
        duration: const Duration(milliseconds: 180),
        curve: Curves.easeOut,
      );
    });
  }

  String time() {
    final now = DateTime.now();
    String number(int value) => value.toString().padLeft(2, '0');
    return '${number(now.hour)}:${number(now.minute)}:${number(now.second)}';
  }

  @override
  void dispose() {
    scroll.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    final canInstall = ready && !installBusy && !installed;
    final canUninstall = ready && !installBusy && installed;

    final canStart = ready && !runBusy && !running;
    final canStop = ready && !runBusy && running;

    return Scaffold(
      body: SafeArea(
        child: Center(
          child: Padding(
            padding: const EdgeInsets.all(16),
            child: ConstrainedBox(
              constraints: const BoxConstraints(
                maxWidth: 720,
                maxHeight: 580,
              ),
              child: Column(
                children: [
                  _header(),
                  const SizedBox(height: 14),
                  LayoutBuilder(
                    builder: (context, constraints) {
                      final panels = [
                        _featurePanel(
                          title: '自由移动',
                          description: installed ? '当前已启用' : '解除棋子移动限制',
                          icon: Icons.open_with_rounded,
                          active: installed,
                          busy: installBusy,
                          enableLabel: '启用',
                          disableLabel: '关闭',
                          canEnable: canInstall,
                          canDisable: canUninstall,
                          onEnable: () => send(
                            'Open free move',
                            installGroup: true,
                            onSuccess: () => installed = true,
                          ),
                          onDisable: () => send(
                            'Close free move',
                            installGroup: true,
                            onSuccess: () => installed = false,
                          ),
                        ),
                        _featurePanel(
                          title: '移动即赢',
                          description: running ? '当前已启用' : '移动后立即获胜',
                          icon: Icons.bolt_rounded,
                          active: running,
                          busy: runBusy,
                          enableLabel: '启用',
                          disableLabel: '关闭',
                          canEnable: canStart,
                          canDisable: canStop,
                          onEnable: () => send(
                            'Open win directly',
                            installGroup: false,
                            onSuccess: () => running = true,
                          ),
                          onDisable: () => send(
                            'Close win directly',
                            installGroup: false,
                            onSuccess: () => running = false,
                          ),
                        ),
                      ];

                      if (constraints.maxWidth < 560) {
                        return Column(
                          children: [
                            panels[0],
                            const SizedBox(height: 10),
                            panels[1],
                          ],
                        );
                      }

                      return Row(
                        children: [
                          Expanded(child: panels[0]),
                          const SizedBox(width: 10),
                          Expanded(child: panels[1]),
                        ],
                      );
                    },
                  ),
                  const SizedBox(height: 12),
                  Expanded(child: _logBox()),
                ],
              ),
            ),
          ),
        ),
      ),
    );
  }

  Widget _header() {
    return Row(
      children: [
        Container(
          width: 38,
          height: 38,
          decoration: BoxDecoration(
            color: const Color(0xffe9eafd),
            borderRadius: BorderRadius.circular(10),
          ),
          child: const Icon(
            Icons.tune_rounded,
            size: 20,
            color: Color(0xff5965d6),
          ),
        ),
        const SizedBox(width: 11),
        const Expanded(
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              Text(
                '国际象棋两项修改器',
                style: TextStyle(
                  fontSize: 18,
                  fontWeight: FontWeight.w600,
                  letterSpacing: -0.2,
                ),
              ),
              SizedBox(height: 1),
              Text(
                '本地服务  ·  127.0.0.1:27654',
                style: TextStyle(
                  fontSize: 11.5,
                  color: Color(0xff737785),
                ),
              ),
            ],
          ),
        ),
        _statusBadge(),
      ],
    );
  }

  Widget _statusBadge() {
    final background = ready
        ? const Color(0xffeaf7ef)
        : const Color(0xfffff4df);
    final foreground = ready
        ? const Color(0xff287a48)
        : const Color(0xff9a6414);

    return Container(
      padding: const EdgeInsets.symmetric(horizontal: 9, vertical: 5),
      decoration: BoxDecoration(
        color: background,
        borderRadius: BorderRadius.circular(99),
      ),
      child: Row(
        mainAxisSize: MainAxisSize.min,
        children: [
          Container(
            width: 6,
            height: 6,
            decoration: BoxDecoration(
              color: foreground,
              shape: BoxShape.circle,
            ),
          ),
          const SizedBox(width: 5),
          Text(
            ready ? '就绪' : '准备中',
            style: TextStyle(
              color: foreground,
              fontSize: 11,
              fontWeight: FontWeight.w600,
            ),
          ),
        ],
      ),
    );
  }

  Widget _featurePanel({
    required String title,
    required String description,
    required IconData icon,
    required bool active,
    required bool busy,
    required String enableLabel,
    required String disableLabel,
    required bool canEnable,
    required bool canDisable,
    required VoidCallback onEnable,
    required VoidCallback onDisable,
  }) {
    return DecoratedBox(
      decoration: BoxDecoration(
        color: Colors.white,
        borderRadius: BorderRadius.circular(12),
        border: Border.all(color: const Color(0xffe2e4ea)),
      ),
      child: Padding(
        padding: const EdgeInsets.all(12),
        child: Column(
          children: [
            Row(
              children: [
                Container(
                  width: 32,
                  height: 32,
                  decoration: BoxDecoration(
                    color: active
                        ? const Color(0xffe9eafd)
                        : const Color(0xfff1f2f5),
                    borderRadius: BorderRadius.circular(8),
                  ),
                  child: Icon(
                    icon,
                    size: 18,
                    color: active
                        ? const Color(0xff5965d6)
                        : const Color(0xff6f7380),
                  ),
                ),
                const SizedBox(width: 9),
                Expanded(
                  child: Column(
                    crossAxisAlignment: CrossAxisAlignment.start,
                    children: [
                      Row(
                        children: [
                          Flexible(
                            child: Text(
                              title,
                              maxLines: 1,
                              overflow: TextOverflow.ellipsis,
                              style: const TextStyle(
                                fontSize: 14,
                                fontWeight: FontWeight.w600,
                              ),
                            ),
                          ),
                          const SizedBox(width: 6),
                          if (busy) ...[
                            const SizedBox(width: 1),
                            const SizedBox(
                              width: 12,
                              height: 12,
                              child: CircularProgressIndicator(strokeWidth: 1.6),
                            ),
                          ] else
                            Text(
                              active ? '已开启' : '已关闭',
                              style: TextStyle(
                                fontSize: 10.5,
                                fontWeight: FontWeight.w600,
                                color: active
                                    ? const Color(0xff4f5ccd)
                                    : const Color(0xff8a8e99),
                              ),
                            ),
                        ],
                      ),
                      const SizedBox(height: 1),
                      Text(
                        description,
                        maxLines: 1,
                        overflow: TextOverflow.ellipsis,
                        style: const TextStyle(
                          fontSize: 11.5,
                          color: Color(0xff7b7f8b),
                        ),
                      ),
                    ],
                  ),
                ),
              ],
            ),
            const SizedBox(height: 11),
            Row(
              children: [
                Expanded(
                  child: FilledButton.icon(
                    onPressed: canEnable ? onEnable : null,
                    icon: const Icon(Icons.play_arrow_rounded, size: 17),
                    label: Text(enableLabel),
                  ),
                ),
                const SizedBox(width: 8),
                Expanded(
                  child: OutlinedButton.icon(
                    onPressed: canDisable ? onDisable : null,
                    icon: const Icon(Icons.stop_rounded, size: 16),
                    label: Text(disableLabel),
                  ),
                ),
              ],
            ),
          ],
        ),
      ),
    );
  }

  Widget _logBox() {
    return DecoratedBox(
      decoration: BoxDecoration(
        color: Colors.white,
        borderRadius: BorderRadius.circular(12),
        border: Border.all(color: const Color(0xffe2e4ea)),
      ),
      child: Column(
        children: [
          SizedBox(
            height: 40,
            child: Padding(
              padding: const EdgeInsets.symmetric(horizontal: 12),
              child: Row(
                children: [
                  const Icon(
                    Icons.terminal_rounded,
                    size: 17,
                    color: Color(0xff686c78),
                  ),
                  const SizedBox(width: 7),
                  const Text(
                    '运行日志',
                    style: TextStyle(
                      fontSize: 13,
                      fontWeight: FontWeight.w600,
                    ),
                  ),
                  const Spacer(),
                  Text(
                    '${logs.length} 条',
                    style: const TextStyle(
                      fontSize: 10.5,
                      color: Color(0xff9296a0),
                    ),
                  ),
                ],
              ),
            ),
          ),
          const Divider(),
          Expanded(
            child: logs.isEmpty
                ? const Center(
                    child: Text(
                      '暂无日志',
                      style: TextStyle(
                        fontSize: 12,
                        color: Color(0xff9a9da6),
                      ),
                    ),
                  )
                : ListView.builder(
                    controller: scroll,
                    padding: const EdgeInsets.fromLTRB(12, 10, 12, 12),
                    itemCount: logs.length,
                    itemBuilder: (_, index) => Padding(
                      padding: const EdgeInsets.only(bottom: 4),
                      child: SelectableText(
                        logs[index],
                        style: TextStyle(
                          fontFamily:
                              Platform.isWindows ? 'Cascadia Mono' : 'monospace',
                          fontFamilyFallback: const [
                            'Consolas',
                            'Microsoft YaHei UI',
                          ],
                          fontSize: 11.5,
                          height: 1.35,
                          color: const Color(0xff40434c),
                        ),
                      ),
                    ),
                  ),
          ),
        ],
      ),
    );
  }
}