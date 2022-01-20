# esp32 OTA
在esp32 OTA（esp-idf v4.3）例程中，默认采用三个分区，factory，ota_0，ota_1，但在实际使用中，并没能充分的使用有限的flash资源。很多情况下，稍微使用多一些功能（如WIFI、网络通信等），程序占用空间很容易就超过1M，如继续按例程的分区方式将会遇到空间不足的问题。

在实际使用中，往往采用以下方式（当然并不是唯一的）即把分区分成，引导区、配置区、应用程序（包含两个）、数据保存区等，即factory分区将被删除，合并到APP区功能。

## 1.配置

### (1)配置分区表为用户配置模式
```
CONFIG_ESPTOOLPY_FLASHSIZE_4MB=y
CONFIG_PARTITION_TABLE_CUSTOM=y
CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions_ota_user.csv"
CONFIG_PARTITION_TABLE_FILENAME="partitions_ota_user.csv"
```
CONFIG_ESPTOOLPY_FLASHSIZE_4MB=y不能缺少，指定flash空间大小。

### (2)分区表信息
```
nvs,      data, nvs,     ,        0x4000,
otadata,  data, ota,     ,        0x2000,
phy_init, data, phy,     ,        0x1000,
ota_0,    app,  ota_0,   ,        1800K,
ota_1,    app,  ota_1,   ,        1800K,
storage,  data, spiffs,  ,        64K,

```

## 2.调试
### (1)查看程序启动版本
在程序启动成功后，将会有启动地址和版本信息，查看OTA升级是否成功。
```
I (4969) USER_OTA: Running partition type 0 subtype 16 (offset 0x00010000)
I (4979) USER_OTA: Running firmware version: 1.0.0.1
I (4979) USER_OTA: create_ota_bussiness running...
```
编译时修改version.txt中的版本号
```
1.0.0.1
```


### (2)升级过程
升级成功
```
I (4969) USER_OTA: Running partition type 0 subtype 16 (offset 0x00010000)
I (4979) USER_OTA: Running firmware version: 1.0.0.3
I (4979) USER_OTA: create_ota_bussiness running...
I (34989) USER_OTA: OTA_UPDATE_BIT, created_ota_queue_http_task:0
I (34989) USER_OTA: ota_queue_http_task starting...
I (34989) USER_OTA: Partition info configured:65536 ruunig:65536
I (34989) USER_OTA: Running partition type 0 subtype 16 (offset 0x00010000)
I (34999) USER_OTA: Writing to partition subtype 17 at offset 0x1e0000
I (39929) USER_OTA: OTA_RUN_HTTP_CLIENT_BIT get.
I (39929) USER_OTA: Starting ota http client...
I (40009) USER_OTA: HTTP_EVENT_ON_CONNECTED
I (40019) USER_OTA: HTTP_EVENT_HEADER_SENT
I (40069) USER_OTA: file info Content-Length:865712
I (40069) USER_OTA: File total size: (865712 Byte), download(0 Byte : 0%)
I (40129) USER_OTA: File total size: (865712 Byte), download(9840 Byte : 1%)
I (40179) USER_OTA: File total size: (865712 Byte), download(19680 Byte : 2%)
... ...
I (44349) USER_OTA: File total size: (865712 Byte), download(848752 Byte : 98%)
I (44389) USER_OTA: File total size: (865712 Byte), download(858592 Byte : 99%)
I (44409) USER_OTA: HTTP_EVENT_ON_FINISH
I (44409) USER_OTA: HTTP_EVENT_DISCONNECTED
I (44419) USER_OTA: HTTP_EVENT_DISCONNECTED
I (44429) USER_OTA: File total size: (865712 Byte), download(865712 Byte : 100%)
I (39929) USER_OTA: run_ota_http_client:0
I (44429) esp_image: segment 0: paddr=001e0020 vaddr=3f400020 size=1fc58h (130136) map
I (44479) esp_image: segment 1: paddr=001ffc80 vaddr=3ffb0000 size=00398h (   920) 
I (44479) esp_image: segment 2: paddr=00200020 vaddr=400d0020 size=99d20h (630048) map
I (44689) esp_image: segment 3: paddr=00299d48 vaddr=3ffb0398 size=03fe0h ( 16352) 
I (44699) esp_image: segment 4: paddr=0029dd30 vaddr=40080000 size=1583ch ( 88124) 
I (44729) esp_image: segment 5: paddr=002b3574 vaddr=50000000 size=00010h (    16) 
I (44729) esp_image: segment 0: paddr=001e0020 vaddr=3f400020 size=1fc58h (130136) map
I (44769) esp_image: segment 1: paddr=001ffc80 vaddr=3ffb0000 size=00398h (   920) 
I (44779) esp_image: segment 2: paddr=00200020 vaddr=400d0020 size=99d20h (630048) map
I (44979) esp_image: segment 3: paddr=00299d48 vaddr=3ffb0398 size=03fe0h ( 16352) 
I (44989) esp_image: segment 4: paddr=0029dd30 vaddr=40080000 size=1583ch ( 88124) 
I (45019) esp_image: segment 5: paddr=002b3574 vaddr=50000000 size=00010h (    16) 
W (45069) USER_OTA: OTA_UPDATE_SUCCESS envet get. 
System will restart later.
I (48069) wifi:state: run -> init (0)
```

重启后输出的信息如下，版本已经更新。
```
I (3969) USER_OTA: Running partition type 0 subtype 17 (offset 0x001e0000)
I (3979) USER_OTA: Running firmware version: 1.0.0.2
I (3979) USER_OTA: create_ota_bussiness running...
```

升级失败或超时
```
I (3989) USER_OTA: Running partition type 0 subtype 17 (offset 0x001e0000)
I (3989) USER_OTA: Running firmware version: 1.0.0.2
I (3999) USER_OTA: create_ota_bussiness running...
I (34009) USER_OTA: OTA_UPDATE_BIT, created_ota_queue_http_task:0
I (34009) USER_OTA: ota_queue_http_task starting...
I (34009) USER_OTA: Partition info configured:1966080 ruunig:1966080
I (34009) USER_OTA: Running partition type 0 subtype 17 (offset 0x001e0000)
I (34019) USER_OTA: Writing to partition subtype 16 at offset 0x10000
I (39089) USER_OTA: OTA_RUN_HTTP_CLIENT_BIT get.
I (39089) USER_OTA: Starting ota http client...
I (39149) USER_OTA: HTTP_EVENT_ON_CONNECTED
I (39149) USER_OTA: HTTP_EVENT_HEADER_SENT
I (39199) USER_OTA: file info Content-Length:865712
I (39199) USER_OTA: File total size: (865712 Byte), download(0 Byte : 0%)
I (39249) USER_OTA: File total size: (865712 Byte), download(9500 Byte : 1%)
I (39299) USER_OTA: File total size: (865712 Byte), download(18940 Byte : 2%)
I (39349) USER_OTA: File total size: (865712 Byte), download(28780 Byte : 3%)
I (39399) USER_OTA: File total size: (865712 Byte), download(38028 Byte : 4%)
I (39449) USER_OTA: File total size: (865712 Byte), download(47500 Byte : 5%)
I (39499) USER_OTA: File total size: (865712 Byte), download(57148 Byte : 6%)
I (39539) USER_OTA: File total size: (865712 Byte), download(66380 Byte : 7%)
I (39589) USER_OTA: File total size: (865712 Byte), download(75612 Byte : 8%)
I (39629) USER_OTA: File total size: (865712 Byte), download(84444 Byte : 9%)
I (39679) USER_OTA: File total size: (865712 Byte), download(94284 Byte : 10%)
I (39739) USER_OTA: File total size: (865712 Byte), download(103516 Byte : 11%)
I (39789) USER_OTA: File total size: (865712 Byte), download(113164 Byte : 13%)
I (39829) USER_OTA: File total size: (865712 Byte), download(122812 Byte : 14%)
I (39879) USER_OTA: File total size: (865712 Byte), download(132252 Byte : 15%)
I (39879) USER_OTA: HTTP_EVENT_ON_FINISH
I (39879) USER_OTA: HTTP_EVENT_DISCONNECTED
I (39889) USER_OTA: HTTP_EVENT_DISCONNECTED
I (39089) USER_OTA: run_ota_http_client:0
E (54889) USER_OTA: OTA_TIMEOUT_NONRESPONE_BIT envet get. 
System update fail and will restart later.
I (57889) wifi:state: run -> init (0)
I (57889) wifi:pm stop, total sleep time: 230851 us / 55059165 us  
```

## 3.实际使用中需要考虑的问题
```
1、OTA方式数据流是其它方式，不一定为http获取文件的方式；
2、固件文件的安全性，即对固件文件进行加密；
3、对flash进行加密；
4、修改bootloader，增加解密加载flash区的固件；
```