# 快捷键 / Keyboard shortcuts

下表取自本构建源码的快捷键定义（`src/ng/tests/ShortcutParse_ut.cpp` 中的已文档化列表）。
上游把主键修饰记作 `Ctrl`；在 macOS 构建里 **Command (⌘) 就是程序内部的 Ctrl 修饰键**，
所以 `Ctrl + O` 在键盘上按 `⌘O`。单键快捷键需在画布获得焦点时使用（不是文本输入框）。

The table is taken from this build's own shortcut definitions. On macOS the Command key maps to the
application's Ctrl modifier, so `Ctrl + O` in the source means `⌘O` on the keyboard.

## 文件 / File

| 上游写法 | macOS 按键 | 动作 |
|---|---|---|
| Ctrl + O | ⌘O | 打开文件 / Open file |
| Ctrl + N | ⌘N | 新窗口 / New window |
| Ctrl + W / Ctrl + F4 | ⌘W | 关闭当前文档 / Close document |
| Ctrl + S | ⌘S | 另存为 / Save as |
| Ctrl + P | ⌘P | 打印 / Print |
| Ctrl + D | ⌘D | 文档属性 / Properties |
| Ctrl + Q | ⌘Q | 退出 / Quit |
| F2 | F2 | 重命名文件 / Rename file |
| r | r | 重新加载文档 / Reload |
| q | q | 关闭当前文档 / Close current document |

## 查找与导航 / Find and navigate

| 上游写法 | macOS 按键 | 动作 |
|---|---|---|
| Ctrl + F | ⌘F | 查找 / Find |
| F3 / Shift + F3 | F3 / ⇧F3 | 下一个 / 上一个匹配 |
| Ctrl + G | ⌘G | 跳转到页 / Go to page |
| g | g | 跳转到页 / Go to page |
| Home / End | Home / End | 首页 / 末页 |
| Alt + Left / Alt + Right | ⌥← / ⌥→ | 后退 / 前进 |
| n / p | n / p | 下一页 / 上一页 |
| Space / Shift + Space | Space / ⇧Space | 下翻 / 上翻一页 |
| Ctrl + B | ⌘B | 加入收藏 / Add favorite |

## 视图与缩放 / View and zoom

| 上游写法 | macOS 按键 | 动作 |
|---|---|---|
| Ctrl + 6 / 7 / 8 | ⌘6 / ⌘7 / ⌘8 | 单页 / 双页 / 书籍视图 |
| Ctrl + 0 / 1 / 2 / 3 | ⌘0 / ⌘1 / ⌘2 / ⌘3 | 适合整页 / 实际大小 / 适合宽度 / 适合内容 |
| Ctrl + Y | ⌘Y | 自定义缩放 / Custom zoom |
| F11 / Ctrl + Shift + L | F11 / ⌘⇧L | 全屏 / Fullscreen |
| F5 / Ctrl + L | F5 / ⌘L | 演示模式 / Presentation |
| Shift + F11 | ⇧F11 | 演示模式 / Presentation |
| F12 / F8 / F9 | F12 / F8 / F9 | 书签 / 工具栏 / 菜单栏 |
| c / z / i | c / z / i | 连续滚动 / 缩放切换 / 页面信息 |
| [ / ] | [ / ] | 左转 / 右转页面 |
| Shift + i | ⇧i | 反色 / Invert colors |

## 标签与杂项 / Tabs and misc

| 上游写法 | macOS 按键 | 动作 |
|---|---|---|
| Ctrl + Tab / Ctrl + Shift + Tab | ⌘Tab / ⌘⇧Tab | 下一个 / 上一个标签 |
| Ctrl + PageDown / PageUp | ⌘PageDown / ⌘PageUp | 下一个 / 上一个标签 |
| Ctrl + Shift + N | ⌘⇧N | 复制到新窗口 |
| Ctrl + Shift + Left / Right | ⌘⇧← / ⌘⇧→ | 同目录上一个 / 下一个文件 |
| Ctrl + K | ⌘K | 命令面板 / Command palette |
| Ctrl + A / Ctrl + C | ⌘A / ⌘C | 全选 / 复制 |
| F1 | F1 | 打开或关闭本手册 |
| F6 / F7 | F6 / F7 | 切换焦点 / 键盘选择文本 |

## 自定义 / Customization

所有快捷键都可以在 `Settings → Advanced Settings` 中通过 `Shortcuts` 覆盖；
格式示例：`Ctrl-Alt-F`。命令名见上游 [Commands] 页面（联网）。
`⌘K` 的命令面板可以在不记快捷键的情况下直接搜索并执行命令。
