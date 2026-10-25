<div align="center">

# Foobar2000 Plugin Suite

**让音乐库、播放控制、歌词与音频设置拥有统一、清晰的体验**

莫兰迪蓝视觉 · 模块化组件 · foobar2000 2.x x64

</div>

------------------------------------------------------------------------

## 项目简介

**Foobar2000 Plugin Suite** 是一套面向 foobar2000
的二次开发组件，采用模块化设计，将专辑浏览、正在播放信息、歌词交互、音频设置和播放控制整合到统一的桌面布局中。

项目由多个独立 DLL 组件组成，旨在保留 foobar2000
灵活、可定制的特点，同时让日常音乐管理与播放操作更加直观、美观。

## 功能一览

| 组件                      | 主要功能                                                                           |
|---------------------------|------------------------------------------------------------------------------------|
| `foo_pro_album_grid`      | 专辑封面网格浏览与搜索；查看专辑曲目，并可将曲目拖入播放列表                       |
| `foo_pro_nowplaying`      | 展示当前正在播放的歌曲信息                                                         |
| `foo_pro_lyrics`          | 在线歌词搜索、本地缓存、模糊匹配、歌词时间轴同步；支持点击或拖动歌词跳转播放位置   |
| `foo_easy_audio_settings` | 管理音频输出设备与解码器相关设置                                                   |
| `foo_pro_transport`       | 独立播放控制面板，支持上一曲、播放、暂停、停止、下一曲、播放顺序切换及可拖动进度条 |

## 运行环境

- **播放器：** foobar2000 2.x
- **架构：** Windows x64
- **开发环境：** Visual Studio 2019（VS2019）
- **安装文件：** `Release_x64` 目录中的 DLL 文件

> 请使用与当前 foobar2000
> 版本及架构相匹配的组件。本文中的路径为示例，请按实际安装位置调整。

## 安装方式

安装前，请先**完全退出
foobar2000**，再根据自己的安装方式选择以下一种路径。不要将同一个组件重复安装到多个目录。

### 方式 A：安装到 components 目录

1.  找到 foobar2000 的安装目录。
2.  从项目的 `Release_x64` 目录中提取所需 DLL。
3.  按当前安装方式，将 DLL 放入 foobar2000 的 `components` 目录。
4.  重新启动 foobar2000。

示例路径：

``` text
D:\\HCTT\\foobar2000\\components\\
```

### 方式 B：安装到用户组件目录

如果你的 foobar2000 配置使用 `user-components-x64`
目录，可按以下结构放置组件：

``` text
user-components-x64/
├── foo_easy_audio_settings/
│   └── foo_easy_audio_settings.dll
├── foo_pro_nowplaying/
│   └── foo_pro_nowplaying.dll
├── foo_pro_lyrics/
│   └── foo_pro_lyrics.dll
├── foo_pro_album_grid/
│   └── foo_pro_album_grid.dll
└── foo_pro_transport/
    └── foo_pro_transport.dll
```

示例路径：

``` text
D:\\HCTT\\foobar2000\\profile\\user-components-x64\\
```

如果你的配置目录与示例不同，请使用实际的 foobar2000 用户配置路径。

## 将组件添加到主界面

安装 DLL 后，需要在 foobar2000 的布局编辑模式中添加对应的 UI 组件。

1.  启动 foobar2000。
2.  打开菜单 **View → Layout → Enable Layout Editing Mode**。
3.  在希望放置组件的区域单击右键。
4.  选择 **Replace UI Element**（替换当前界面元素）或 **Add New UI
    Element**（添加新界面元素）。
5.  在组件列表中选择对应的插件。
6.  根据需要调整面板大小与位置。
7.  布局完成后，再次关闭 **Enable Layout Editing
    Mode**，避免误操作布局。

## 推荐布局

以下是一个可供参考的布局示意，可根据屏幕尺寸和使用习惯调整：

``` text
┌──────────────────┬──────────────────┬──────────────────┐
│                  │                  │                  │
│  Pro Album Grid  │     Playlist     │ Pro Now Playing  │
│                  │                  ├──────────────────┤
│                  │                  │                  │
│                  │                  │    Pro Lyrics    │
├──────────────────┤                  │                  │
│  Audio Settings  │                  │                  │
├──────────────────┴──────────────────┴──────────────────┤
│                    Pro Transport                       │
└────────────────────────────────────────────────────────┘
```

**布局建议：**

- 左侧放置专辑封面浏览与音频设置。
- 中间保留较大的播放列表区域。
- 右侧放置正在播放信息和歌词面板。
- 底部放置播放控制面板，便于随时操作。

## 常见问题

### 安装后没有看到组件

- 确认已完全退出并重新启动 foobar2000。
- 检查 DLL 是否放入当前配置实际使用的组件目录。
- 确认组件为 x64 版本，并与播放器版本匹配。
- 检查是否误将 DLL 放入了另一个 foobar2000 安装目录。

### 组件已安装，但主界面没有显示

组件安装后，通常还需要通过 **Layout Editing Mode**
将其添加到布局中。请依照“将组件添加到主界面”一节操作。

### 如何恢复正常布局操作

完成面板添加与调整后，关闭 **Enable Layout Editing Mode**
即可退出布局编辑模式。

## 项目结构

``` text
Release_x64/
├── foo_easy_audio_settings.dll
├── foo_pro_nowplaying.dll
├── foo_pro_lyrics.dll
├── foo_pro_album_grid.dll
└── foo_pro_transport.dll
```

实际构建产物可能随项目版本变化，请以当前发布目录中的文件为准。

## 设计目标

- **模块化：** 各功能以独立 DLL 组织，便于按需添加。
- **统一视觉：** 以莫兰迪蓝为整体风格，减少不同面板之间的视觉割裂。
- **交互清晰：**
  将专辑浏览、歌词查看、播放控制和音频设置放在易于理解的位置。
- **保留可定制性：** 用户可以根据自己的音乐库与使用习惯安排 foobar2000
  布局。

------------------------------------------------------------------------

<div align="center">

**Foobar2000 Plugin Suite**  
让音乐管理更清晰，让播放体验更统一。

</div>
