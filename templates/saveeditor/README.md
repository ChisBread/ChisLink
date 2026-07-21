# Save editor templates / 存档编辑模板

Copy this directory to `/sd/.chislink/saveeditor/` on the ChisLink SD card.

将本目录复制到 ChisLink SD 卡的 `/sd/.chislink/saveeditor/`。

## Matching rules / 匹配规则

When a `.sav` file is selected in Manager, the template is matched from the selected save file's containing directory, not from the inserted cartridge.

Manager checks templates in this order:

1. Full directory name: `/sd/.chislink/saveeditor/{directory}.conf`
2. Last four uppercase alphanumeric characters of the directory: `/sd/.chislink/saveeditor/{gamecode}.conf`

For the standard save directory `{title}_{gamecode}`, the second rule loads a template such as `BZME.conf`.

在 Manager 中选择 `.sav` 文件时，模板根据该存档文件所在目录匹配，而不是根据当前插入的卡带匹配。

Manager 按以下顺序查找模板：

1. 完整目录名：`/sd/.chislink/saveeditor/{目录名}.conf`
2. 目录名最后 4 个大写字母/数字：`/sd/.chislink/saveeditor/{gamecode}.conf`

标准存档目录为 `{游戏名}_{gamecode}`，因此通常会命中类似 `BZME.conf` 的模板。

## File format / 文件格式

Templates are UTF-8 text files. Each non-empty line is `key=value`. Lines beginning with `#` or `;` are comments. Decimal and `0x` hexadecimal numbers are accepted.

模板是 UTF-8 文本文件。每个非空配置行使用 `key=value`。以 `#` 或 `;` 开头的行是注释。数字支持十进制和 `0x` 十六进制。

Localized labels use `en:Name|zh:名字`. Manager chooses the current UI language, then falls back to English.

本地化标签使用 `en:Name|zh:名字`。Manager 优先使用当前界面语言，缺失时回退到英文。

## Common keys / 通用字段

- `version=1|2`: Template format version. Use `2` for new templates.
- `transform=none|reverse8`: Save byte transform. `reverse8` reverses every 8-byte physical block into logical order.
- `writable=0|1`: Whether Manager may write edited values back. Use `0` until selector, checksum, and linked fields are fully understood.
- `slots=<count>` and `slot_stride=<bytes>`: Simple fixed-slot v1/v2 layout.
- `selector=...`: v2 slot/copy selector for more complex saves.
- `slot_label=<label>,string|type,<offset>,<length>`: Read-only slot display value, such as a character name or save counter.
- `field=<label>,<type>,<offset>,<min>,<max>,<step>[,<bit_offset>,<bit_width>]`: Editable scalar or bitfield.
- `checksum=<algorithm>,<target_offset>,<type>,<start>,<end>[,<initial>,<skip_offset>]`: v2 checksum rule.
- `magic=<offset>,<stride>,<ASCII>`: Legacy fixed-slot validity marker.

- `version=1|2`：模板格式版本。新模板请使用 `2`。
- `transform=none|reverse8`：存档字节变换。`reverse8` 会把每个 8 字节物理块反转成逻辑顺序。
- `writable=0|1`：是否允许 Manager 写回修改。没有完全确认槽位选择、校验和、联动字段前请使用 `0`。
- `slots=<数量>` 和 `slot_stride=<字节>`：简单固定槽位布局，可用于 v1/v2。
- `selector=...`：v2 的槽位/副本选择规则，用于更复杂的存档。
- `slot_label=<标签>,string|类型,<偏移>,<长度>`：只读槽位显示值，例如角色名或保存次数。
- `field=<标签>,<类型>,<偏移>,<最小>,<最大>,<步进>[,<位偏移>,<位宽>]`：可编辑标量或位字段。
- `checksum=<算法>,<目标偏移>,<类型>,<开始>,<结束>[,<初值>,<跳过偏移>]`：v2 校验规则。
- `magic=<偏移>,<步长>,<ASCII>`：旧版固定槽位有效性标记。

## Value types / 数值类型

Supported integer types are `u8`, `u16`, `u24`, `u32`, `be16`, `be24`, and `be32`.

Bitfields are extracted from the raw integer with `bit_offset` and `bit_width`. For example:

```text
field=en:Sound Test|zh:声音测试,u8,0x1B,0,1,1,5,1
```

This edits bit 5 of byte `0x1B` as a 0/1 value.

支持的整数类型为 `u8`、`u16`、`u24`、`u32`、`be16`、`be24`、`be32`。

位字段通过 `bit_offset` 和 `bit_width` 从原始整数中提取。例如：

```text
field=en:Sound Test|zh:声音测试,u8,0x1B,0,1,1,5,1
```

这表示把 `0x1B` 字节的第 5 位作为 0/1 字段编辑。

## Selectors / 槽位选择器

Fixed slots:

```text
selector=fixed,<slot_count>,<base>,<slot_stride>
```

Latest copy by counter:

```text
selector=latest,<slot_count>,<scan_base>,<scan_stride>,<scan_count>,<magic_offset>,<magic|none>,<counter_offset>,<counter_type>
```

Last matching copy:

```text
selector=last_magic,<slot_count>,<scan_base>,<scan_stride>,<scan_count>,<magic_offset>,<magic>
```

Physical page by slot id:

```text
selector=slot_id,<slot_count>,<scan_base>,<scan_stride>,<scan_count>,<magic_offset>,<magic|none>,<id_offset>,<id_type>
```

`magic=none` skips the marker check and scans by counter or slot id only.

固定槽位：

```text
selector=fixed,<槽位数>,<基址>,<槽位步长>
```

按计数器选择最新副本：

```text
selector=latest,<槽位数>,<扫描基址>,<扫描步长>,<扫描数量>,<magic偏移>,<magic|none>,<计数器偏移>,<计数器类型>
```

选择最后一个匹配副本：

```text
selector=last_magic,<槽位数>,<扫描基址>,<扫描步长>,<扫描数量>,<magic偏移>,<magic>
```

按槽位 id 选择物理页：

```text
selector=slot_id,<槽位数>,<扫描基址>,<扫描步长>,<扫描数量>,<magic偏移>,<magic|none>,<id偏移>,<id类型>
```

`magic=none` 表示不检查标记，只按计数器或槽位 id 扫描。

## Checksums / 校验和

Supported algorithms: `minish`, `byte_sum`, `byte_sum_xor`, `word_sum`, `word_sub`, `advance_wars`, `kingdom_hearts`, `fire_emblem`, `word_nonzero`, `wario_sum`, and `wario_complement`.

The checksum range is `[start, end)`, relative to the selected data base. When saving, Manager writes zero to checksum targets first, recalculates each rule, then writes the new checksum value.

支持的算法：`minish`、`byte_sum`、`byte_sum_xor`、`word_sum`、`word_sub`、`advance_wars`、`kingdom_hearts`、`fire_emblem`、`word_nonzero`、`wario_sum`、`wario_complement`。

校验范围为 `[start, end)`，相对于选中的数据基址。保存时 Manager 会先把校验目标写 0，再重新计算并写入新值。

## Write safety / 写入安全

Set `writable=1` only when all of these are true:

- The selector always points to the active save data.
- Every edited field can be changed independently.
- All affected checksums are represented.
- The save is not inside an unsupported compressed or packed container.

Use `writable=0` for view-only templates. This still helps users inspect slot names, counters, progress flags, and values without risking save corruption.

只有同时满足以下条件时才使用 `writable=1`：

- 选择器总能指向当前有效存档数据。
- 每个编辑字段都可以独立修改。
- 所有受影响的校验和都已经配置。
- 存档不在当前引擎不支持的压缩或打包容器中。

只读模板请使用 `writable=0`。它仍然可以帮助用户查看槽名、保存次数、进度标记和数值，同时避免损坏存档。

## Creating a template / 自行编写模板

1. Identify the game's four-character GBA game code, such as `BZME`.
2. Create `/sd/.chislink/saveeditor/BZME.conf`.
3. Start with `version=2`, `transform=none`, and `writable=0`.
4. Add `selector=fixed` or the smallest selector that can find the active slot.
5. Add a few read-only-safe `field=` lines and test that values display correctly.
6. Add checksum rules before enabling writes.
7. Set `writable=1` only after testing a backup copy of the save.

1. 找到游戏的 4 位 GBA gamecode，例如 `BZME`。
2. 创建 `/sd/.chislink/saveeditor/BZME.conf`。
3. 从 `version=2`、`transform=none`、`writable=0` 开始。
4. 添加 `selector=fixed`，或能找到有效槽位的最小选择器。
5. 先添加少量安全的 `field=` 并确认显示正确。
6. 启用写入前先补齐校验规则。
7. 只在备份存档上测试通过后再设为 `writable=1`。

## Included templates / 内置模板

Writable templates:

- `AAME`, `AAMJ`, `AAMP`: Castlevania: Circle of the Moon
- `AFFE`, `AFFJ`, `AFFP`: Final Fight One
- `AKWE`, `AKWP`: Konami Krazy Racers
- `ASOE`, `ASOJ`, `ASOP`: Sonic Advance
- `A2NE`, `A2NJ`, `A2NP`: Sonic Advance 2
- `B3SE`, `B3SJ`, `B3SP`: Sonic Advance 3
- `AWAE`: Wario Land 4
- `AXRE`, `AXRJ`, `AXRP`: Super Street Fighter II Turbo Revival
- `AZWE`, `AZWP`: WarioWare, Inc.
- `BZME`, `BZMJ`, `BZMP`: The Legend of Zelda: The Minish Cap

View-only templates:

- `AWRE`, `AWRP`: Advance Wars
- `A2CE`, `A2CJ`, `A2CP`: Castlevania: Aria of Sorrow
- `ACHE`, `ACHJ`, `ACHP`: Castlevania: Harmony of Dissonance
- `AFZC`, `AFZE`, `AFZJ`: F-Zero: Maximum Velocity
- `BZ6E`, `BZ6J`, `BZ6P`: Final Fantasy VI Advance
- `AE7E`, `AE7J`, `AE7X`, `AE7Y`: Fire Emblem: The Blazing Blade
- `AGSE`, `AGSJ`, `AGSD`, `AGSF`, `AGSI`, `AGSS`: Golden Sun
- `AGFE`, `AGFJ`, `AGFD`, `AGFF`, `AGFI`, `AGFS`: Golden Sun: The Lost Age
- `B8CE`, `B8CJ`, `B8CP`: Kingdom Hearts: Chain of Memories
- `AKRJ`, `AKRP`: Kuru Kuru Kururin

可写模板：

- `AAME`、`AAMJ`、`AAMP`：恶魔城 Circle of the Moon
- `AFFE`、`AFFJ`、`AFFP`：快打旋风 One
- `AKWE`、`AKWP`：Konami Krazy Racers
- `ASOE`、`ASOJ`、`ASOP`：索尼克 Advance
- `A2NE`、`A2NJ`、`A2NP`：索尼克 Advance 2
- `B3SE`、`B3SJ`、`B3SP`：索尼克 Advance 3
- `AWAE`：瓦力欧大陆 4
- `AXRE`、`AXRJ`、`AXRP`：超级街霸 II Turbo Revival
- `AZWE`、`AZWP`：瓦力欧制造
- `BZME`、`BZMJ`、`BZMP`：塞尔达传说 缩小帽

只读模板：

- `AWRE`、`AWRP`：高级战争
- `A2CE`、`A2CJ`、`A2CP`：恶魔城 晓月圆舞曲
- `ACHE`、`ACHJ`、`ACHP`：恶魔城 白夜协奏曲
- `AFZC`、`AFZE`、`AFZJ`：F-Zero Maximum Velocity
- `BZ6E`、`BZ6J`、`BZ6P`：最终幻想 VI Advance
- `AE7E`、`AE7J`、`AE7X`、`AE7Y`：火焰纹章 烈火之剑
- `AGSE`、`AGSJ`、`AGSD`、`AGSF`、`AGSI`、`AGSS`：黄金太阳 开启的封印
- `AGFE`、`AGFJ`、`AGFD`、`AGFF`、`AGFI`、`AGFS`：黄金太阳 失落的时代
- `B8CE`、`B8CJ`、`B8CP`：王国之心 记忆之链
- `AKRJ`、`AKRP`：转转棒

## Attribution / 来源说明

Several game layouts and field ideas were ported from RyudoSynbios' `game-tools-collection` project, which is distributed under the MIT License. ChisLink templates are adapted to Manager's compact runtime format and may intentionally be read-only when the full upstream write behavior is not represented yet.

部分游戏布局和字段思路移植自 RyudoSynbios 的 `game-tools-collection` 项目，该项目使用 MIT License。ChisLink 模板已适配 Manager 的轻量运行时格式；当上游完整写入行为尚未被当前模板规则表达时，这些模板会故意保持只读。
