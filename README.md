# Foobar2000plugin
一套面向日常音乐管理与播放体验的二次开发组件，整体采用莫兰迪蓝风格并针对主界面进行了模块化设计，主要包括专辑封面浏览与搜索、专辑曲目查看及拖入播放列表、专业 Now Playing 正在播放展示、在线歌词搜索与本地缓存、支持模糊匹配和时间轴同步的歌词面板、可点击/拖动跳转的歌词交互、音频输出设备与解码器管理，以及独立的播放控制面板，可实现上一曲、播放、暂停、停止、下一曲、播放顺序切换和可拖动进度条等功能；整个套件以多个独立 DLL 组成，适配 foobar2000 2.x x64 和 VS2019，目标是在保留 foobar2000 高度可定制特性的同时，让音乐库浏览、歌曲播放、歌词查看和音频设置更加直观、统一和美观\n
正常使用提取Release_x64的dll放在foobar2000的***\foobar2000\components即可\n
安装插件
参考你的 foobar2000 安装目录：
例如：D:\HCTT\foobar2000

建议先完全关闭 foobar2000。
将各 DLL 分别放入：
D:\HCTT\foobar2000\profile\user-components-x64\

推荐目录结构：
user-components-x64
│
├─ foo_easy_audio_settings
│  └─ foo_easy_audio_settings.dll
│
├─ foo_pro_nowplaying
│  └─ foo_pro_nowplaying.dll
│
├─ foo_pro_lyrics
│  └─ foo_pro_lyrics.dll
│
├─ foo_pro_album_grid
│  └─ foo_pro_album_grid.dll
│
└─ foo_pro_transport
   └─ foo_pro_transport.dll

然后重新打开 foobar2000。

把插件添加到主界面
进入：
View
→ Layout
→ Enable Layout Editing Mode

然后在需要放置插件的位置右键：
Replace UI Element

或者：
Add New UI Element

找到对应组件。
建议布局：
┌──────────────┬──────────────────┬──────────────────┐
│              │                  │                  │
│ Pro Album    │    Playlist      │ Pro Now Playing  │
│ Grid         │                  │                  │
│              │                  ├──────────────────┤
│              │                  │                  │
│              │                  │ Pro Lyrics       │
├──────────────┤                  │                  │
│ Audio        │                  │                  │
│ Settings     │                  │                  │
├──────────────┴──────────────────┴──────────────────┤
│                  Pro Transport                    │
└───────────────────────────────────────────────────┘

设置完成后关闭：
Enable Layout Editing Mode

防止以后误拖动布局。
