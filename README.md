<div align="center">

# blink-installer

### 应用打磨了半年，用户第一眼看到的，是一个 1998 年的灰色向导。

**用 HTML、CSS、JavaScript 写 Windows 安装包界面。**

[![npm](https://img.shields.io/npm/v/blink-installer?color=%2312b866&label=npm)](https://www.npmjs.com/package/blink-installer)
[![license](https://img.shields.io/badge/license-MIT-blue)](LICENSE)
![platform](https://img.shields.io/badge/Windows-7%20%E2%86%92%2011-0078d4)
![size](https://img.shields.io/badge/%E6%A1%86%E6%9E%B6%E5%BC%80%E9%94%80-6%20MB-lightgrey)

[English](README.en.md)

![全屏入场动画收敛为安装卡片](docs/demo.gif)

</div>

---

## 受够了吧

- 想挪一个按钮，得在 `nsDialogs` 里手算像素坐标，改一次编译一次
- 想加个入场动画——没有这个东西，NSIS 的界面模型里压根不存在
- 全世界的 Inno Setup 安装包长得一模一样，你的产品也一样
- 弹个「确定要退出吗」，出来的是 Windows 95 同款灰底方框
- 改一行文案，要先学一门只有写安装包才用得上的 DSL
- 前端同事想帮忙？帮不上，这里没有 CSS

**而你已经会写网页了。**

```html
<button class="primary" id="install">开始安装</button>
```

```js
import { installer, fs } from 'blink-installer-ui';

document.querySelector('#install').onclick = () => installer.begin();
installer.on('progress', ({ percent }) => bar.style.width = percent + '%');
```

就这样。圆角、渐变、投影、`transition`、全屏动画、你自己的字体——**都是你平时怎么写就怎么写**。

```
npm i -D blink-installer
npx blink-installer init
npx blink-installer build
```

---

## 凭什么能做到

压缩、解压、快捷方式、卸载注册这些脏活由 **NSIS** 干，界面交给 **miniblink** 渲染（一个 Blink 内核，单文件 DLL，Win7 也能跑）。

关键在于窗口是**透明分层窗口**：所以你能有圆角、投影，以及直接画在桌面上、四周全透明的全屏动画——这些普通 Win32 安装程序做不到。

而且动画和安装界面**是同一个窗口**。它从全屏收缩成卡片，不是切到第二个进程，所以画面不跳、不闪、不重载。

---

## 它做什么

它不打包你的应用，只把**已经打好包的目录**套成安装程序：

```
已打包目录（electron-builder 的 win-unpacked / electron-packager 输出 / 一个 exe 加若干 dll）
        +  你的 HTML 界面
        +  一份配置
        ─────────────────────────▶  MyApp-Setup-1.2.3.exe
```

## 快速开始

```js
// blink-installer.config.mjs
import { defineConfig } from 'blink-installer-core';

export default defineConfig({
  appId: 'com.example.myapp',
  productName: '我的应用',
  version: '1.2.3',
  publisher: '某某公司',

  source: 'dist/win-unpacked',   // 已打包好的目录
  exe: 'myapp.exe',

  ui: './installer-ui',          // 省略则用自带模板
  splash: { enabled: true },     // 全屏入场动画

  install: {
    defaultDir: '$LOCALAPPDATA\\Programs\\我的应用',
    elevate: false,              // 每用户安装，免 UAC
    shortcuts: { desktop: true, startMenu: true },
  },
});
```

`npx blink-installer init --eject-ui` 会把自带模板拷到你项目里，直接改。

### 两种构建模式

压缩载荷占掉了绝大部分构建时间，而调界面时这部分工作跟你无关。所以分两条命令：

```json
"installer:dev":     "blink-installer build --ui-only --compression zlib --out dist/preview.exe",
"installer:release": "blink-installer build --compression lzma"
```

`--ui-only` 把应用换成占位文件，其余全是真的（真 NSIS 脚本、真页面、真进度、真卸载器），能跑完整流程。**它还会自动隔离**：装到独立目录、独立注册表键、关掉快捷方式，所以预览包不可能覆盖你已装的正式版。

实测（1.1 GB 的 Electron 应用）：

| | 耗时 | 体积 |
|---|---|---|
| `--ui-only --compression zlib` | 3 秒 | 13 MB |
| `--compression zlib` | 79 秒 | 496 MB |
| `--compression lzma` | 450 秒 | 401 MB |

发布用 `lzma`——electron-builder 自带的 NSIS 也是这个，选别的就是你俩体积差异的来源。

---

## 让 AI agent 帮你集成

把下面整段复制到 Claude Code / Cursor / Codex 之类的编码 agent 里，它会读懂你的项目，然后生成配套的动画、对话框和安装流程。

> 提示语里带了这个渲染引擎的全部硬约束。少了它们，agent 会写出在普通浏览器里好好的、到安装包里却空白或塌掉的界面。

````markdown
请为本项目集成 blink-installer，做一个用 HTML/CSS/JS 写界面的 Windows 安装包。

## 第一步：先读懂这个项目
在动手之前，先看 README、package.json、主进程入口和产品文案，弄清楚：
这个产品是做什么的、卖点是什么、面向谁、有没有品牌色和图标、名字与版本号从哪里取。
安装界面和动画必须由这些**具体内容**长出来，不要套通用模板。

## 第二步：装并初始化
npm i -D blink-installer
npx blink-installer init --eject-ui

## 第三步：写配置 blink-installer.config.mjs
- version 和 productName 从 package.json 现算，不要写死（写死必然漂）
- source 指向已打包目录（electron-builder 是 dist/<out>/win-unpacked）
- exe 是该目录下的主程序文件名
- 装 $LOCALAPPDATA 就设 elevate: false（免 UAC，体验好得多）；装 $PROGRAMFILES 才需要 true
- output 若要替换现有安装包，就对齐原来的产物命名规则

## 第四步：设计界面（重点）
在 installer-ui/ 下做三样东西，都要贴合这个产品：

1. **入场动画**（splash.js / splash.css）
   全屏、透明、画在桌面上。用产品自己的意象，别用无意义的粒子。
   例如协作类产品可以让多个节点汇聚成一个中心；工具类可以让零件组装成形。
   时长 2~3 秒，结束后调 win.setBounds 把窗口收成安装卡片。

2. **安装流程界面**（index.html / main.js / style.css）
   欢迎页讲清楚这个产品是什么、装完能干什么；
   带安装路径选择与磁盘空间校验；安装中显示进度并轮播产品能力；完成页给下一步动作。

3. **对话框**
   用 ui.messageBox，它是页内绘制的，通过 --bk-dialog-* 变量调成产品配色。
   退出确认、覆盖安装确认、卸载原因询问都走它。

## 渲染引擎的硬约束（务必遵守，否则静默失效）
渲染器是 Chromium 57~60 时代的 Blink：
- CSS 不能用：inset（改 top/right/bottom/left）、flex 的 gap（改 margin）、
  accent-color、aspect-ratio、:is()/:where()、flex 子项上的 position: sticky
- JS 语法会被 Babel 自动降级，可以正常写；但 Object.entries 和 String.padStart
  是运行时缺失、编译不掉，绝对不要用
- **U+FFFF 以上的字符渲染为空白**。绝大多数彩色 emoji 在此列（🕑 U+1F551、
  💬 U+1F4AC 都是空白），只能用基本多文种平面内的符号（✋ U+270B、✉ U+2709 可以），
  或者内联 SVG
- 呈现帧率硬上限约 33fps。动画要用大幅度、慢速、带缓动的运动，避免细碎快速位移
- 尺寸一律写**逻辑像素**，原生层会按系统缩放自动放大并缩放页面，不要自己做 DPI 补偿
- 透明窗口下 window.innerWidth 返回 0，要屏幕尺寸就调 sys.screen()

## 第五步：验证
package.json 加两条命令：
  "installer:dev":     "blink-installer build --ui-only --compression zlib --out dist/preview.exe"
  "installer:release": "blink-installer build --compression lzma"
跑 installer:dev（几秒出包），实际运行产物，把整个流程点一遍再交付。
````

---

## 写界面

页面就是普通网页，通过一套带类型的 API 和安装程序对话：

```js
import { installer, config, fs, proc, win, ui } from 'blink-installer-ui';

const dir = await fs.pickDirectory({ title: '选择安装位置' });
config.set('installDir', dir);

installer.on('progress', ({ percent }) => bar.style.width = percent + '%');
installer.on('log', ({ message }) => detail.textContent = message);

document.querySelector('#install').onclick = () => installer.begin();
```

### 对话框

`ui.messageBox` 画在页面里，不走 Win32，所以确认框和界面其余部分是一套东西：

```js
const answer = await ui.messageBox({
  title: '退出安装？',
  message: '安装正在进行中，此时退出会留下不完整的安装。',
  buttons: 'yesNo',
  icon: 'warning',
});
if (answer === 'yes') win.close(true);
```

三层控制：默认样式直接能用（按钮文案跟随系统语言）→ 覆盖 `--bk-dialog-surface`、`--bk-dialog-accent` 等变量调色 → 重写 `.bk-dialog*` 类，或用 `ui.setDialogRenderer(fn)` 整个接管。

`ui.messageBoxNative` 保留了原生弹窗，用于页面还没法绘制的早期失败。

### 屏幕缩放

配置和 CSS 里写的都是**逻辑像素**。安装程序启动时读一次系统缩放，据此定窗口大小并缩放页面，所以 `width: 880` 和 `font-size: 14px` 在 150% 的笔记本和 100% 的台式机上观感一致，不需要媒体查询，也不要自己补偿。

`sys.screen()` 返回的同样是逻辑像素，`win.setBounds` / `win.resize` 收的也是，所以拿屏幕矩形和 CSS 尺寸一起做运算不会错位。真需要设备像素时用 `sys.screen().scale`。

### 入场动画

`splash: { enabled: true }` 让窗口先铺满工作区、带逐像素透明地播一段动画，没画到的地方就是透明的，桌面透出来；播完同一个窗口收成安装卡片。

**帧率**：miniblink 的呈现被硬限制在约 **33fps**，而 `requestAnimationFrame` 以 59Hz 触发——页面算出的帧约有一半从未上屏。这个调不动：五个 miniblink 版本（2017~2021）、各种窗口尺寸、开不开逐像素透明、改 `drawMinInterval`，结果都一样。`native/test/FINDINGS.md` 有测量数据。

按这个来设计：大幅、慢速、带拖尾和辉光的运动在 33fps 下很好看；细碎的快速位移会顿。把动画循环跑得更快只是烧 CPU。

### 引擎老，这点很重要

miniblink 报的是 `Chrome/60`，但 JS 解析器比这更旧。实测**没有**解构、默认参数、对象展开、`async`/`await`、类字段、`?.`、`??`、`**`、可选 catch 绑定；**有**类、生成器、箭头函数、`let`、模板字符串、`Promise`、`Map`/`Set`、`for...of`。

你不用管：构建链先 esbuild 再 Babel，上面这些全部降级，正常写现代 JavaScript 即可。只有两个是运行时缺失、编译不掉——**别用 `Object.entries` 和 `String.padStart`**。

CSS 不做转译，避开 flex 子项上的 `position: sticky`，2017 年之后的特性用前先确认。

内容上还有一个坑：**U+FFFF 以上的字符画不出来**。绝大多数彩色 emoji 在那个区间，`🕑`(U+1F551)、`💬`(U+1F4AC) 是空白，而 `✋`(U+270B)、`✉`(U+2709) 正常。图标非要用字符就限制在基本多文种平面内，否则用内联 SVG。

---

## 配置

| 字段 | 含义 |
| --- | --- |
| `appId` | 反向域名标识，用于卸载注册表项 |
| `productName` / `version` / `publisher` | 界面与文件属性里显示 |
| `source` | 要安装的已打包目录 |
| `exe` | 主程序，相对 `source` |
| `output` | 安装包路径，默认 `dist/<product>-Setup-<version>.exe` |
| `ui` | 含 `index.html` 的目录，省略则用自带模板 |
| `icon` | setup.exe 与「程序和功能」里的 `.ico` |
| `window` | `width`、`height`、`transparent` |
| `splash` | `enabled`、`timeoutMs`（动画卡住时的兜底） |
| `install.defaultDir` | 可用 `$PROGRAMFILES`、`$LOCALAPPDATA` 等 NSIS 变量 |
| `install.elevate` | `true` 需管理员、装全机（HKLM）；`false` 为每用户（HKCU） |
| `install.shortcuts` | `desktop`、`startMenu`、`startMenuFolder` |
| `install.uninstallEntry` | 生成卸载程序并注册 |
| `install.legacyFolderPicker` | 强制用 XP 时代的目录树选择框，见下 |
| `uninstall.ui` | 卸载程序也用你的 HTML 界面（默认 `true`） |
| `compression` | `lzma`（最小）、`bzip2`、`zlib`（最快） |
| `sign` | 代码签名，省略则不签 |
| `nsis.include` | 注入生成脚本的 `.nsh`，逃生舱 |

### 关于 `install.legacyFolderPicker`

选目录默认用资源管理器那个对话框（`IFileOpenDialog`）：有地址栏、有收藏夹、能直接粘路径。打开这个开关会换成 XP 时代那个 400 像素宽的目录树框（`SHBrowseForFolder`）。

**这是逃生舱，不是风格选项。** 现代对话框由 shell 托管，也就等于把进程暴露给这台机器上加载的一切 shell 扩展——遇到某个第三方扩展让它卡住或打不开的时候，老的树框反而还能用，因为它对 shell 的要求少得多。手上有确切复现再打开它，不要因为"想要复古"。

页面也可以单次覆盖，不用改构建：

```js
await fs.pickDirectory({ title: '选择安装位置', legacy: true });
```

两条路径的取消行为一致：**取消就结束**，不会再弹第二个对话框。

## 代码签名

不签名的安装包每次下载都会触发 SmartScreen 警告，直到足够多人点过「仍要运行」。

如果你已经在用 electron-builder，多半已经有一个签名钩子，而这里的形状和它的 `win.signtoolOptions.sign` **完全一致**——直接指过去：

```js
sign: { hook: './electron/sign-win.cjs' }
```

签名这套东西会沉淀很多踩出来的细节（云证书没有可导出的私钥、会话中途过期、无人值守构建答不了的弹窗要靠超时兜住）。复用同一个钩子，就不会有两份实现慢慢走岔。

没有现成钩子就直接驱动 signtool。按指纹从证书store里取（云/HSM 证书唯一可行的方式）：

```js
sign: { thumbprint: 'C3C1…91ED', timestamp: 'http://time.certum.pl' }
```

或者用证书文件，密码走环境变量而不是写进配置：

```js
sign: { certificateFile: './cert.pfx' }   // BLINK_SIGN_PASSWORD=…
```

默认签不上就大声警告、照常出不签名的包，这样会话过期也还有东西可测。正式发版设 `sign.required: true`，让它直接失败。

**卸载程序不签名**：NSIS 是在用户机器上安装时才写出它的，构建期不存在这个文件。要签得做两遍构建；而 SmartScreen 判定的是用户下载的那个安装包。

## 卸载程序

默认和安装程序一套待遇：你的 HTML、确认页、进度、完成页。代价是安装目录里要常驻一份渲染器（约 17 MB）供卸载时用。`uninstall: { ui: false }` 关掉，退回 NSIS 自带对话框，磁盘上不留额外东西。

## 静默安装

`/S` 跳过所有界面，`/D=路径` 指定安装目录（必须是最后一个参数，且不加引号）：

```
MyApp-Setup.exe /S /D=C:\Tools\MyApp
```

## 集成方式

**Electron Forge**：

```js
makers: [{ name: 'blink-installer-maker', config: { /* 同上面的配置 */ } }]
```

**electron-builder**：先 `electron-builder --dir` 出 `win-unpacked`，再让 `blink-installer build` 接手。

**传统 Win32**：`source` 指向你放 exe 和 dll 的目录即可。

## 从源码构建

```
npm install
npm run build              # TypeScript 各包
npm run native:configure   # CMake，x86
npm run native:build       # 产出 blinkkit.dll
npm test
```

需要 Visual Studio（含 C++ 桌面开发工作负载）与 CMake。运行时二进制已随仓库提交，所以克隆下来离线也能构建。

## 许可

本项目 MIT。分发的第三方二进制按各自许可：miniblink 为 Apache-2.0，NSIS 为 zlib/libpng（其 LZMA 模块为 CPL-1.0）。详见 [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md)。
