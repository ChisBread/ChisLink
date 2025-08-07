# ChisLink GBA全功能便携烧录器使用说明书

## 一、安全须知
### 防砖备份（关键操作）
1. **首次使用准备**  
   - 使用符合MBR分区表、单分区格式的TF卡
   - 确保`/config`目录存在`signature.bin`文件
   - **必须备份此文件**至安全位置

2. **刷固件规范**  
   - 每次刷写前需确认TF卡内含正确的`signature.bin`
   - 缺失此文件会导致设备变砖

## 二、设备启动
| 场景 | 启动方式 |
|------|----------|
| GBA卡槽无卡 | 自动启动Link或自定义App |
| 卡槽有卡 | 按住`START+SELECT`组合键 |

## 三、卡带管理
### 1. 卡带信息识别
**格式示例**：  
`ChisFlash-J-32M-F1M`  
- **字段解析**：
  - **ChisFlash**：兼容卡带类型，ChisFlash/Bootleg/Cartridge
  - **J/S**：NOR Flash型号缩写
  - **32M**：最大支持32MB(256Mb)容量
  - **S/F/B/E(1M)**：存档类型标识（SRAM/Flash/免电/EEPROM）

### 2. 存档管理
| 功能 | 操作说明 |
|------|----------|
| **备份(烧录卡)** | 自动识别存档类型备份至`/saves/游戏名/backup.sav` |
| **备份(原版卡)** | 依据数据库规则备份（防误识别） |
| **存档载入** | 正版卡请选择"原版卡"模式 |

### 3. 特殊功能
- **合卡菜单**：  
  - 按`SELECT`切换菜单视图
  - 按`A`选定游戏，进行存档、ROM管理
- **硬件详情**：  
  `L+R`组合键查看（兼容性报告需提供此信息）

### 4. ROM备份
- 备份当前卡带

## 四、文件系统
### 目录结构

根目录/
├── saves/        # 存档目录
├── favorites/    # 收藏夹
└── config/       # 系统配置
└── dumps/        # ROM备份


### 文件操作
| 操作 | 限制条件 |
|------|----------|
| 自动重命名 | 仅限.gba文件（自动识别ROM名称） |
| 移动/删除 | 不支持文件夹操作 |
| 快捷键 | `START`切换后缀显示，左右方向键翻页 |

## 五、WiFi配置
### 连接模式
| 配置类型 | 路径/参数 | 说明 |
|----------|-----------|------|
| **内置配置** | SSID: Bach<br>密码: chisbread | 修改后即时生效 |
| **TF卡配置** | `/config/web.conf` | 更新固件后不会被覆盖 |

### 高级设置

- /config/web.conf

```ini
wifi_disable=0          # 禁用WiFi
wifi_lazy_connect=1     # 推荐启用（降低功耗）
wifi_max_power_adjust=0 # 功率调节范围(-16~16)
```

## 六、进阶功能
### 超频烧录
- **启用方式**：主界面按`START`键
- **生效标识**：右下角显示`@Bread`（大写B）
- **注意事项**：
  - GBM全系列不支持超频
  - 校验失败需关闭超频

## 七、鸣谢
- Butano
- devkitPro
- gba-link-connection

## 八、其它

### 固件升级
1. 通过USB连接电脑
2. 访问 [esptool-js页面](https://chisbread.github.io/esptool-js/)
3. 选择`USB JTAG/serial`完成烧录


> 注：本产品支持ChisFlash MBC5系列卡带，GBC需专用转接板（非标配）