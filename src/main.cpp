//////////////////////////////////////////////////////////////////////////////////
// 中景园 0.96" OLED 4针 I2C 版 —— ESP32-C3 SuperMini + PlatformIO
//
// 接线（4针 OLED -> ESP32-C3 SuperMini）：
//   GND -> GND
//   VCC -> 3.3V
//   SCL -> GPIO6
//   SDA -> GPIO7
//
// 4针 I2C 模块只有 SCL/SDA，无 RES/DC/CS；I2C 地址 0x3C。
// 使用硬件 I2C（Wire 库），400kHz；SCL/SDA 由 Wire.begin() 配置，勿再 pinMode。
//
// 其他外设（与 7 针工程相同）：
//   触摸模块 I/O -> GPIO1
//   震动马达 IN  -> GPIO3
//////////////////////////////////////////////////////////////////////////////////
#include <Arduino.h>
#include <string.h>
#include <Wire.h>
#include "font.h"
#include "daichi_gundam.h"
#include "daichi_intro.h"
#include "relaxed.h"
#include "angry.h"
#include "laugh.h"
#include "embarrassed.h"
#include "proud.h"

int scl=6;//SCL
int sda=7;//SDA
int touchPin=1;//触摸模块 I/O 输出
int vibratePin=3;//震动马达驱动板 IN 控制脚（摸头时振动）

#define OLED_SCLK_Clr() digitalWrite(scl,LOW)//SCL
#define OLED_SCLK_Set() digitalWrite(scl,HIGH)

#define OLED_SDIN_Clr() digitalWrite(sda,LOW)//SDA
#define OLED_SDIN_Set() digitalWrite(sda,HIGH)

#define OLED_CMD  0  //写命令
#define OLED_DATA 1 //写数据

uint8_t OLED_GRAM[128][8];//将要显示的缓存内容

//前置声明（GCC 需要：setup()/loop() 里调用了定义在后面的函数）
void OLED_Init(void);
void OLED_ColorTurn(uint8_t i);
void OLED_DisplayTurn(uint8_t i);
void OLED_WR_Byte(uint8_t dat,uint8_t cmd);
void OLED_Refresh(void);
void OLED_Clear(void);
void OLED_ShowPicture(uint8_t x0,uint8_t y0,uint8_t x1,uint8_t y1,const uint8_t BMP[]);
void OLED_ShowChinese(uint8_t x,uint8_t y,const uint8_t num,uint8_t size1);
void OLED_ShowString(uint8_t x,uint8_t y,const char *chr,uint8_t size1);
void OLED_ShowChar(uint8_t x,uint8_t y,const char chr,uint8_t size1);
void OLED_ShowNum(uint8_t x,uint8_t y,int num,uint8_t len,uint8_t size1);
uint32_t OLED_Pow(uint8_t m,uint8_t n);
void OLED_DrawPoint(uint8_t x,uint8_t y);

// 播放一个 GIF：逐帧解包像素写入 OLED 显存，刷新后按帧延时等待
// expectTouch=true 表示播放期间应保持按住；触摸状态一偏离就中断返回，实现最快响应
// 帧数据格式见 daichi_gundam.h / daichi_intro.h：1 bit/像素，行优先、MSB 在前
void playGIF(const AnimatedGIF* gif, uint16_t loopCount, bool expectTouch)
{
  for (uint16_t loop = 0; loop < loopCount; loop++) {
    for (uint8_t frame = 0; frame < gif->frame_count; frame++) {
      // 逐帧中断检测：状态一改就停止当前动画，立即切到另一端
      if ((digitalRead(touchPin) == HIGH) != expectTouch) return;

      // 清空显存（不触发刷新，避免多余 SPI 传输）
      memset(OLED_GRAM, 0, sizeof(OLED_GRAM));

      // 逐个像素解包：命中则该 Bit 置 1
      for (uint16_t y = 0; y < gif->height; y++) {
        uint16_t rowIndex = y * ((gif->width + 7) / 8);
        for (uint16_t x = 0; x < gif->width; x++) {
          uint16_t byteIndex = rowIndex + (x / 8);
          uint8_t  bitIndex  = 7 - (x % 8);
          if (gif->frames[frame][byteIndex] & (1 << bitIndex)) {
            OLED_DrawPoint(x, y);
          }
        }
      }

      OLED_Refresh();                // 推显存到屏幕
      delay(gif->delays[frame]);     // 帧延时控制帧率
    }
  }
}

// 用 PWM 控制振动强度。percent 0~100，0=停止
// ESP32 GPIO3 挂到 LEDC 通道，8bit 分辨率
// 注意：震动电机是感性负载，PWM 频率要低（几百 Hz）才有足够电流维持转动，
//       5kHz 以上会导致电机振一下就没劲了。
void setVibrate(int percent)
{
  static bool started = false;
  if (!started) {
    ledcSetup(0, 300, 8);            // 通道0, 300Hz, 8bit —— 低频才有持续扭力
    ledcAttachPin(vibratePin, 0);
    started = true;
  }
  ledcWrite(0, (uint32_t)(percent * 255 / 100));// 0~255 占空比
}

// ================= Flappy Bird (游戏/动画双模式) =================
// MODE_PET  : 摸头动画。短按=摸头(laugh+振动)，长按3秒=进游戏菜单。
// MODE_MENU : 游戏选择菜单。短按=光标下移，长按2秒=确认进入高亮游戏。
// MODE_PLAY : 按住上升、松开下降。穿过缺口得分，撞柱/顶/地死亡。
// MODE_OVER : 死亡后短按重玩，长按3秒=返回宠物模式。
enum GameMode { MODE_PET = 0, MODE_MENU, MODE_PLAY, MODE_OVER };
GameMode gMode = MODE_PET;

// 游戏 ID：菜单光标选中哪个，长按确认后进对应游戏
enum GameId { GAME_FLAPPY = 0, GAME_JUMP };
GameId g_gameId = GAME_FLAPPY;

// 游戏选择菜单：标题 + 光标
#define MAX_GAMES 2
const char* g_gameNames[MAX_GAMES] = { "FLAPPY BIRD", "JUMP JUMP" };
int g_numGames = MAX_GAMES;
int g_cursor = 0;

// 游戏参数（可调，降低难度版）
#define BIRD_X      28
#define BIRD_W      4
#define BIRD_H      4
#define GROUND_Y    60        // 地面 y，鸟底>=此值死亡
#define PIPE_W      9
#define PIPE_GAP    24        // 缺口高度（原18→24更宽，更好穿）
#define PIPE_SPEED  1         // 每帧左移像素（保持1不变，不算太快）
#define PIPE_SPACING 52      // 管道间距（原44→52更稀疏）
#define NUM_PIPES   3
#define FRAME_MS    32        // 每帧毫秒（原30→32略慢，重力节奏放缓）

struct Pipe { int16_t x; int16_t gapY; bool counted; };
Pipe g_pipes[NUM_PIPES];

int16_t g_birdY = 30;
int16_t g_birdVy = 0;
int16_t g_score = 0;
unsigned long g_overStart = 0;   // 死亡时刻，用于防误触重玩

void startGame() {
  g_birdY = 30;
  g_birdVy = 0;
  g_score = 0;
  for (int i = 0; i < NUM_PIPES; i++) {
    g_pipes[i].x = 128 + i * PIPE_SPACING;
    g_pipes[i].gapY = 16 + random(32);      // 缺口中心 16..47
    g_pipes[i].counted = false;
  }
  gMode = MODE_PLAY;
}

void drawFlappyFrame() {
  memset(OLED_GRAM, 0, sizeof(OLED_GRAM));

  // 地面
  for (int xx = 0; xx < 128; xx++)
    for (int yy = GROUND_Y; yy < 64; yy++)
      OLED_DrawPoint(xx, yy);

  // 管道（上下两段）
  for (int i = 0; i < NUM_PIPES; i++) {
    int px = g_pipes[i].x;
    int topY = g_pipes[i].gapY - PIPE_GAP / 2;
    int botY = g_pipes[i].gapY + PIPE_GAP / 2;
    for (int xx = 0; xx < PIPE_W; xx++) {
      if (px + xx < 0 || px + xx > 127) continue;
      for (int yy = 0; yy < topY; yy++)      OLED_DrawPoint(px + xx, yy);
      for (int yy = botY; yy < GROUND_Y; yy++) OLED_DrawPoint(px + xx, yy);
    }
  }

  // 鸟（4x4 方块）
  for (int xx = 0; xx < BIRD_W; xx++)
    for (int yy = 0; yy < BIRD_H; yy++)
      OLED_DrawPoint(BIRD_X + xx, g_birdY + yy);

  // 得分
  OLED_ShowNum(58, 2, g_score, 3, 16);
}

// 碰撞检测：鸟是否撞地/天/柱
bool birdHit() {
  if (g_birdY + BIRD_H - 1 >= GROUND_Y) return true;   // 撞地
  if (g_birdY < 0) return true;                         // 撞天
  int bx0 = BIRD_X, bx1 = BIRD_X + BIRD_W - 1;
  for (int i = 0; i < NUM_PIPES; i++) {
    int px = g_pipes[i].x, px1 = px + PIPE_W - 1;
    if (bx1 < px || bx0 > px1) continue;                // x 无重叠
    int topY = g_pipes[i].gapY - PIPE_GAP / 2;
    int botY = g_pipes[i].gapY + PIPE_GAP / 2;
    if (g_birdY < topY || g_birdY + BIRD_H - 1 > botY) return true;
  }
  return false;
}

// ================= 跳一跳 JUMP JUMP (蓄力松跳, 单向水平) =================
// 按住蓄力(身下出现积分条), 松开 → 小方块按蓄力向前弹跳。
// 落到下一块上持续+1分；落偏/落空 → 死亡。
// 蓄力越久跳得越远，需正好落在下一块。
#define JUMP_PLAYER_W 4
#define JUMP_PLAYER_H 4
#define JUMP_BLOCK_W  10       // 方块宽
#define JUMP_BLOCK_H  3        // 方块高(顶面厚度)
#define JUMP_GAP_MIN  8        // 下一块左边界与当前块右边界的最小间距
#define JUMP_GAP_VAR  6        // 间距随机量
#define JUMP_MAX_CHARGE 20     // 蓄力上限(像素/秒积分换算)
#define JUMP_CHARGE_PER_MS 0.03f // 每ms蓄力像素
#define JUMP_GROUND_Y 56       // 方块所在的基准行

struct JumpState {
  float playerX, playerY;    // 玩家正方形左上角
  float playerVy;
  int   blockX;              // 当前块左边界
  int   targetX;             // 下一块左边界
  float charge;              // 当前蓄力像素
  bool  charging;
  bool  inAir;
  float score;
  float best;
};
JumpState g_jump;

void startJump() {
  g_jump.playerX = 0;
  g_jump.playerY = JUMP_GROUND_Y - JUMP_PLAYER_H;
  g_jump.playerVy = 0;
  g_jump.blockX = 0;
  g_jump.targetX = 10 + random(JUMP_GAP_MIN, JUMP_GAP_MIN + JUMP_GAP_VAR + 1);
  g_jump.charge = 0;
  g_jump.charging = false;
  g_jump.inAir = false;
  g_jump.score = 0;
  g_jump.best = 0;
  gMode = MODE_PLAY;   // 复用 PLAY 状态机，但由游戏ID区分
}

void drawJumpFrame() {
  memset(OLED_GRAM, 0, sizeof(OLED_GRAM));
  // 地面线
  for (int x = 0; x < 128; x++) OLED_DrawPoint(x, JUMP_GROUND_Y);
  // 当前块
  for (int x = g_jump.blockX; x < g_jump.blockX + JUMP_BLOCK_W; x++)
    for (int y = JUMP_GROUND_Y - JUMP_BLOCK_H; y < JUMP_GROUND_Y; y++)
      OLED_DrawPoint(x, y);
  // 下一块
  for (int x = g_jump.targetX; x < g_jump.targetX + JUMP_BLOCK_W; x++)
    for (int y = JUMP_GROUND_Y - JUMP_BLOCK_H; y < JUMP_GROUND_Y; y++)
      OLED_DrawPoint(x, y);
  // 玩家
  for (int x = (int)g_jump.playerX; x < (int)g_jump.playerX + JUMP_PLAYER_W; x++)
    for (int y = (int)g_jump.playerY; y < (int)g_jump.playerY + JUMP_PLAYER_H; y++)
      OLED_DrawPoint(x, y);
  // 蓄力条（玩家脚下）
  if (g_jump.charging) {
    for (int x = 0; x < (int)g_jump.charge && x < 128; x++)
      OLED_DrawPoint(x, JUMP_GROUND_Y - JUMP_BLOCK_H - 1);
  }
  // 得分
  OLED_ShowNum(100, 2, (int)g_jump.score, 3, 12);
}

void updateJumpCharge(float dtMs) {
  if (g_jump.charging) {
    g_jump.charge += JUMP_CHARGE_PER_MS * dtMs;
    if (g_jump.charge > JUMP_MAX_CHARGE) g_jump.charge = JUMP_MAX_CHARGE;
  }
}

void jumpLaunch() {
  // 松开 → 按蓄力水平起跳
  g_jump.playerVy = -3.5f;             // 向上弹
  // 水平推进力 = 蓄力，跑固定秒数
  g_jump.inAir = true;
  g_jump.charging = false;
}

// 返回: 是否仍在空中。落定后判定得分或死亡。
void updateJumpAir(float dtMs) {
  float dx = g_jump.charge * dtMs / 1000.0f * 8.0f;   // 蓄力转水平速度
  g_jump.playerX += dx;
  g_jump.playerVy += 0.25f * dtMs / 16.7f;            // 重力
  g_jump.playerY += g_jump.playerVy * dtMs / 16.7f;

  // 落到地面线
  if (g_jump.playerY + JUMP_PLAYER_H >= JUMP_GROUND_Y) {
    g_jump.playerY = JUMP_GROUND_Y - JUMP_PLAYER_H;
    g_jump.inAir = false;

    // 判定是否落在某块上（块顶平台范围）
    float px0 = g_jump.playerX, px1 = px0 + JUMP_PLAYER_W - 1;
    bool onTarget = (px1 >= g_jump.targetX) && (px0 <= g_jump.targetX + JUMP_BLOCK_W - 1);
    bool onCurrent = (px1 >= g_jump.blockX) && (px0 <= g_jump.blockX + JUMP_BLOCK_W - 1);

    if (onTarget) {
      // 跳到下一块成功：得分，推动
      g_jump.score++;
      g_jump.blockX = g_jump.targetX;
      g_jump.targetX = g_jump.blockX + JUMP_BLOCK_W + random(JUMP_GAP_MIN, JUMP_GAP_MIN + JUMP_GAP_VAR + 1);
      // 若超出屏幕则整屏左移
      if (g_jump.targetX > 80) {
        int shift = g_jump.targetX - 60;
        g_jump.blockX -= shift;
        g_jump.targetX -= shift;
        g_jump.playerX -= shift;
      }
      g_jump.charge = 0;
    } else if (!onCurrent) {
      // 落空 → 死亡
      gMode = MODE_OVER;
      g_overStart = millis();
      setVibrate(80);
    } else {
      // 落回原地(没跳远) → 不扣分，重新蓄力
      g_jump.charge = 0;
    }
  }
}

void jumpMode() {
  if (!g_jump.inAir) {
    // 地面上：按住蓄力
    if (digitalRead(touchPin) == HIGH) {
      g_jump.charging = true;
      updateJumpCharge(FRAME_MS);
    } else {
      if (g_jump.charging) { jumpLaunch(); g_jump.charging = false; }
    }
  } else {
    updateJumpAir(FRAME_MS);
  }

  drawJumpFrame();
  OLED_Refresh();
  delay(FRAME_MS);
}

void petMode() {
  // 不摸：概率动画。expectTouch=false，播放期间一旦按下会即刻中断，保证响应最快。
  if (digitalRead(touchPin) == LOW) {
    uint8_t r = random(10);
    if (r == 0)      playGIF(&embarrassed_gif, 1, false);
    else if (r == 1) playGIF(&proud_gif, 1, false);
    else             playGIF(&angry_gif, 1, false);
    return;
  }

  // 触摸按下：立即开始摸头，不等松开。expectTouch=true → 按住播笑、松手即刻中断(振动同步停)。
  unsigned long t0 = millis();
  setVibrate(45);
  playGIF(&laugh_gif, 1, true);
  setVibrate(0);

  // 若 laugh 播完后仍按着(长按) → 进游戏菜单
  if (digitalRead(touchPin) == HIGH) {
    while (digitalRead(touchPin) == HIGH) {
      if (millis() - t0 >= 3000) {
        gMode = MODE_MENU;
        g_cursor = 0;
        return;
      }
      delay(10);
    }
  }
}

// 游戏选择菜单：短按光标下移，长按2秒确认进入高亮游戏
// 英文标题 GAME + 游戏名 FLAPPY BIRD（中文 8x8 太小糊，改回英文）
void menuMode() {
  memset(OLED_GRAM, 0, sizeof(OLED_GRAM));

  // 标题
  OLED_ShowString(8, 2, "GAME SELECT", 16);
  for (int i = 0; i < g_numGames; i++) {
    // 高亮光标：在选中项前面画一个实心方块标记
    int y = 24 + i * 16;
    if (i == g_cursor) {
      for (int x = 6; x < 12; x++)
        for (int yy = y; yy < y + 8; yy++)
          OLED_DrawPoint(x, yy);
    }
    // 游戏名
    OLED_ShowString(16, y, g_gameNames[i], 12);
  }
  OLED_Refresh();

  // 短按 → 光标下移
  if (digitalRead(touchPin) == HIGH) {
    unsigned long t0 = millis();
    while (digitalRead(touchPin) == HIGH) {
      if (millis() - t0 >= 2000) {          // 长按确认 → 进入高亮游戏
        if (g_cursor == 0) { g_gameId = GAME_FLAPPY; startGame(); }
        else              { g_gameId = GAME_JUMP;   startJump(); }
        return;
      }
      delay(10);
    }
    // 松开 = 短按：下移一格
    g_cursor++;
    if (g_cursor >= g_numGames) g_cursor = 0;
    delay(150);
  }
}

void setup()
{
  Wire.begin(sda, scl);              // 硬件 I2C：SDA=GPIO7, SCL=GPIO6
  Wire.setClock(400000);             // 400kHz（加速刷屏；pinMode 的坑已修，可放心用）
  Wire.setBufferSize(160);           // 放大发送缓冲，整页 129 字节一次发出
  OLED_Init();
  OLED_ColorTurn(0);//0正常显示
  OLED_DisplayTurn(0);//0正常显示
  pinMode(touchPin, INPUT_PULLUP);// 点动高电平输出，内部上拉防悬空
  setVibrate(0);// 默认关闭振动
  randomSeed(esp_random());// 用硬件随机源种子
}

void loop()
{
  if (gMode == MODE_PET) {
    petMode();
  } else if (gMode == MODE_MENU) {
    menuMode();
  } else if (gMode == MODE_PLAY) {
    // 按游戏 ID 区分：Flappy 或 跳一跳
    if (g_gameId == GAME_JUMP) {
      jumpMode();
    } else {
      // 按住上升，松开下降
      if (digitalRead(touchPin) == HIGH) {
        g_birdVy -= 1;
      } else {
        g_birdVy += 1;
      }
      // 限速（放宽到 ±3 但增量更平缓，鸟更容易控制）
      if (g_birdVy > 3) g_birdVy = 3;
      if (g_birdVy < -3) g_birdVy = -3;
      g_birdY += g_birdVy;

      // 管道移动
      for (int i = 0; i < NUM_PIPES; i++) {
        g_pipes[i].x -= PIPE_SPEED;
        if (g_pipes[i].x < -PIPE_W) {
          g_pipes[i].x = 128 + PIPE_SPACING - PIPE_W;
          g_pipes[i].gapY = 16 + random(32);
          g_pipes[i].counted = false;
        }
        // 计分
        if (!g_pipes[i].counted && g_pipes[i].x + PIPE_W < BIRD_X) {
          g_pipes[i].counted = true;
          g_score++;
        }
      }

      drawFlappyFrame();
      OLED_Refresh();

      if (birdHit()) {
        gMode = MODE_OVER;
        g_overStart = millis();
        setVibrate(80);            // 撞一下，短振提示
      } else {
        delay(FRAME_MS);
      }
    }   // end of GAME_FLAPPY else-branch
  } else if (gMode == MODE_OVER) {
    setVibrate(0);
    // 死亡画面
    memset(OLED_GRAM, 0, sizeof(OLED_GRAM));
    OLED_ShowString(16, 12, "GAME OVER", 16);
    // 分数按当前游戏 ID 取
    int sc = (g_gameId == GAME_JUMP) ? (int)g_jump.score : g_score;
    OLED_ShowNum(56, 34, sc, 3, 16);
    OLED_ShowString(4, 50, "tap restart", 12);
    OLED_Refresh();

    // 长按3秒 → 返回宠物模式；短按 → 重玩
    if (digitalRead(touchPin) == HIGH) {
      unsigned long t0 = millis();
      bool longPress = false;
      while (digitalRead(touchPin) == HIGH) {
        if (millis() - t0 >= 3000) {   // 长按
          longPress = true;
          break;
        }
        delay(10);
      }
      if (longPress) {
        gMode = MODE_PET;
        delay(200);
        setVibrate(0);
      } else {
        // 短按 → 重玩（防误触：从死亡起至少 400ms 才认）
        if (millis() - g_overStart >= 400) {
          if (g_gameId == GAME_JUMP) startJump();
          else                       startGame();
        }
      }
    }
    delay(50);
  }
}
//反显函数
void OLED_ColorTurn(uint8_t i)
{
  if(!i) OLED_WR_Byte(0xA6,OLED_CMD);//正常显示
  else  OLED_WR_Byte(0xA7,OLED_CMD);//反色显示
}

//屏幕旋转180度
void OLED_DisplayTurn(uint8_t i)
{
  if(i==0)
    {
      OLED_WR_Byte(0xC8,OLED_CMD);//正常显示
      OLED_WR_Byte(0xA1,OLED_CMD);
    }
else
    {
      OLED_WR_Byte(0xC0,OLED_CMD);//反转显示
      OLED_WR_Byte(0xA0,OLED_CMD);
    }
}
// ===== 硬件 I2C（用 Wire 库），与 I2C 扫描程序一致，时序可靠 =====
// 向 SSD1306 写一个字节（I2C）
// cmd=0 写命令(控制字节0x00)，cmd=1 写数据(控制字节0x40)
void OLED_WR_Byte(uint8_t dat,uint8_t cmd)
{
  Wire.beginTransmission(0x3C);
  Wire.write(cmd ? 0x40 : 0x00);   // 控制字节
  Wire.write(dat);
  Wire.endTransmission();
}

// 整屏刷显存：按页写入。每页一次事务发 (0x40 + 128数据 = 129字节)，
// Wire 缓冲已放大到 160，够放整页，减少事务开销。
void OLED_Refresh(void)
{
  for(uint8_t i=0;i<8;i++)
  {
    Wire.beginTransmission(0x3C);
    Wire.write(0x00);              // 命令模式
    Wire.write(0xb0+i);            // 页地址
    Wire.write(0x00);              // 列低地址
    Wire.write(0x10);              // 列高地址
    Wire.endTransmission();

    Wire.beginTransmission(0x3C);
    Wire.write(0x40);              // 数据模式
    for(uint8_t n=0;n<128;n++)
      Wire.write(OLED_GRAM[n][i]);
    Wire.endTransmission();
  }
}
//清屏函数
void OLED_Clear(void)
{
  uint8_t i,n;
  for(i=0;i<8;i++)
  {
     for(n=0;n<128;n++)
      {
       OLED_GRAM[n][i]=0;//清除所有数据
      }
  }
  OLED_Refresh();//更新显示
}

//画点
//x:0~127
//y:0~63
void OLED_DrawPoint(uint8_t x,uint8_t y)
{
  uint8_t i,m,n;
  i=y/8;
  m=y%8;
  n=1<<m;
  OLED_GRAM[x][i]|=n;
}

//清除一个点
//x:0~127
//y:0~63
void OLED_ClearPoint(uint8_t x,uint8_t y)
{
  uint8_t i,m,n;
  i=y/8;
  m=y%8;
  n=1<<m;
  OLED_GRAM[x][i]=~OLED_GRAM[x][i];
  OLED_GRAM[x][i]|=n;
  OLED_GRAM[x][i]=~OLED_GRAM[x][i];
}

//画线
//x:0~128
//y:0~64
void OLED_DrawLine(uint8_t x1,uint8_t y1,uint8_t x2,uint8_t y2)
{
  uint8_t i,k,k1,k2,y0;
  if(x1==x2)    //画竖线
  {
      for(i=0;i<(y2-y1);i++)
      {
        OLED_DrawPoint(x1,y1+i);
      }
  }
  else if(y1==y2)   //画横线
  {
      for(i=0;i<(x2-x1);i++)
      {
        OLED_DrawPoint(x1+i,y1);
      }
  }
  else      //画斜线
  {
    k1=y2-y1;
    k2=x2-x1;
    k=k1*10/k2;
    for(i=0;i<(x2-x1);i++)
      {
        OLED_DrawPoint(x1+i,y1+i*k/10);
      }
  }
}
//x,y:圆心坐标
//r:圆的半径
void OLED_DrawCircle(uint8_t x,uint8_t y,uint8_t r)
{
  int a, b,num;
    a = 0;
    b = r;
    while(2 * b * b >= r * r)
    {
        OLED_DrawPoint(x + a, y - b);
        OLED_DrawPoint(x - a, y - b);
        OLED_DrawPoint(x - a, y + b);
        OLED_DrawPoint(x + a, y + b);

        OLED_DrawPoint(x + b, y + a);
        OLED_DrawPoint(x + b, y - a);
        OLED_DrawPoint(x - b, y - a);
        OLED_DrawPoint(x - b, y + a);

        a++;
        num = (a * a + b * b) - r*r;//计算画的点离圆心的距离
        if(num > 0)
        {
            b--;
            a--;
        }
    }
}

//在指定位置显示一个字符,包括部分字符
//x:0~127
//y:0~63
//size:选择字体 12/16/24
//取模方式 逐列式
void OLED_ShowChar(uint8_t x,uint8_t y,const char chr,uint8_t size1)
{
  uint8_t i,m,temp,size2,chr1;
  uint8_t y0=y;
  size2=(size1/8+((size1%8)?1:0))*(size1/2);  //得到字体一个字符对应点阵集所占的字节数
  chr1=chr-' ';  //计算偏移后的值
  for(i=0;i<size2;i++)
  {
    if(size1==12)
        {
          temp=pgm_read_byte(&asc2_1206[chr1][i]);
        } //调用1206字体
    else if(size1==16)
        {
          temp=pgm_read_byte(&asc2_1608[chr1][i]);
        } //调用1608字体
    else if(size1==24)
        {
          temp=pgm_read_byte(&asc2_2412[chr1][i]);
        } //调用2412字体
    else return;
        for(m=0;m<8;m++)           //写入数据
        {
          if(temp&0x80)OLED_DrawPoint(x,y);
          else OLED_ClearPoint(x,y);
          temp<<=1;
          y++;
          if((y-y0)==size1)
          {
            y=y0;
            x++;
            break;
          }
        }
  }
}


//显示字符串
//x,y:起点坐标
//size1:字体大小
//*chr:字符串起始地址
void OLED_ShowString(uint8_t x,uint8_t y,const char *chr,uint8_t size1)
{
  while((*chr>=' ')&&(*chr<='~'))//判断是不是非法字符!
  {
    OLED_ShowChar(x,y,*chr,size1);
    x+=size1/2;
    if(x>128-size1/2)  //换行
    {
      x=0;
      y+=size1;
    }
    chr++;
  }
}

//m^n
uint32_t OLED_Pow(uint8_t m,uint8_t n)
{
  uint32_t result=1;
  while(n--)
  {
    result*=m;
  }
  return result;
}

////显示2个数字
////x,y :起点坐标
////len :数字的位数
////size:字体大小
void OLED_ShowNum(uint8_t x,uint8_t y,int num,uint8_t len,uint8_t size1)
{
  uint8_t t,temp;
  for(t=0;t<len;t++)
  {
    temp=(num/OLED_Pow(10,len-t-1))%10;
      if(temp==0)
      {
        OLED_ShowChar(x+(size1/2)*t,y,'0',size1);
      }
      else
      {
        OLED_ShowChar(x+(size1/2)*t,y,temp+'0',size1);
      }
  }
}

//显示汉字
//x,y:起点坐标
//num:汉字对应的序号
//取模方式 列行式
void OLED_ShowChinese(uint8_t x,uint8_t y,const uint8_t num,uint8_t size1)
{
  uint8_t i,m,n=0,temp,chr1;
  uint8_t x0=x,y0=y;
  uint8_t size3=size1/8;
  while(size3--)
  {
    chr1=num*size1/8+n;
    n++;
      for(i=0;i<size1;i++)
      {
        if(size1==16)
            {temp=pgm_read_byte(&Hzk1[chr1][i]);}//调用16*16字体
        else if(size1==24)
            {temp=pgm_read_byte(&Hzk2[chr1][i]);}//调用24*24字体
        else if(size1==32)
            {temp=pgm_read_byte(&Hzk3[chr1][i]);}//调用32*32字体
        else if(size1==64)
            {temp=pgm_read_byte(&Hzk4[chr1][i]);}//调用64*64字体
        else return;

            for(m=0;m<8;m++)
              {
                if(temp&0x01)OLED_DrawPoint(x,y);
                else OLED_ClearPoint(x,y);
                temp>>=1;
                y++;
              }
              x++;
              if((x-x0)==size1)
              {x=x0;y0=y0+8;}
              y=y0;
       }
  }
}

//配置写入数据的起始位置
void OLED_WR_BP(uint8_t x,uint8_t y)
{
  OLED_WR_Byte(0xb0+y,OLED_CMD);//设置行起始地址
  OLED_WR_Byte(((x&0xf0)>>4)|0x10,OLED_CMD);
  OLED_WR_Byte((x&0x0f),OLED_CMD);
}

//x0,y0：起点坐标
//x1,y1：终点坐标
//BMP[]：要写入的图片数组
void OLED_ShowPicture(uint8_t x0,uint8_t y0,uint8_t x1,uint8_t y1,const uint8_t BMP[])
{
  int j=0;
  uint8_t t;
  uint8_t x,y;
  for(y=y0;y<y1;y++)
   {
     OLED_WR_BP(x0,y);
     for(x=x0;x<x1;x++)
     {
       t=pgm_read_byte(&BMP[j++]);
       OLED_WR_Byte(t,OLED_DATA);
     }
   }
}

//OLED的初始化（I2C 版：只有 SCL/SDA，无 RES/DC/CS）
//注意：SCL/SDA 由 setup() 里的 Wire.begin() 配置，这里不能再 pinMode，
//     否则会把硬件 I2C 外设废掉导致屏幕不亮。
void OLED_Init(void)
{
  // 软件复位：I2C 模块无 RES 脚，直接延时等模块上电稳定
  delay(200);

  OLED_WR_Byte(0xAE,OLED_CMD);//--turn off oled panel
  OLED_WR_Byte(0x00,OLED_CMD);//---set low column address
  OLED_WR_Byte(0x10,OLED_CMD);//---set high column address
  OLED_WR_Byte(0x40,OLED_CMD);//--set start line address  Set Mapping RAM Display Start Line (0x00~0x3F)
  OLED_WR_Byte(0x81,OLED_CMD);//--set contrast control register
  OLED_WR_Byte(0xCF,OLED_CMD);// Set SEG Output Current Brightness
  OLED_WR_Byte(0xA1,OLED_CMD);//--Set SEG/Column Mapping     0xa0左右反置 0xa1正常
  OLED_WR_Byte(0xC8,OLED_CMD);//Set COM/Row Scan Direction   0xc0上下反置 0xc8正常
  OLED_WR_Byte(0xA6,OLED_CMD);//--set normal display
  OLED_WR_Byte(0xA8,OLED_CMD);//--set multiplex ratio(1 to 64)
  OLED_WR_Byte(0x3f,OLED_CMD);//--1/64 duty
  OLED_WR_Byte(0xD3,OLED_CMD);//-set display offset Shift Mapping RAM Counter (0x00~0x3F)
  OLED_WR_Byte(0x00,OLED_CMD);//-not offset
  OLED_WR_Byte(0xd5,OLED_CMD);//--set display clock divide ratio/oscillator frequency
  OLED_WR_Byte(0x80,OLED_CMD);//--set divide ratio, Set Clock as 100 Frames/Sec
  OLED_WR_Byte(0xD9,OLED_CMD);//--set pre-charge period
  OLED_WR_Byte(0xF1,OLED_CMD);//Set Pre-Charge as 15 Clocks & Discharge as 1 Clock
  OLED_WR_Byte(0xDA,OLED_CMD);//--set com pins hardware configuration
  OLED_WR_Byte(0x12,OLED_CMD);
  OLED_WR_Byte(0xDB,OLED_CMD);//--set vcomh
  OLED_WR_Byte(0x40,OLED_CMD);//Set VCOM Deselect Level
  OLED_WR_Byte(0x20,OLED_CMD);//-Set Page Addressing Mode (0x00/0x01/0x02)
  OLED_WR_Byte(0x02,OLED_CMD);//
  OLED_WR_Byte(0x8D,OLED_CMD);//--set Charge Pump enable/disable
  OLED_WR_Byte(0x14,OLED_CMD);//--set(0x10) disable
  OLED_WR_Byte(0xA4,OLED_CMD);// Disable Entire Display On (0xa4/0xa5)
  OLED_WR_Byte(0xA6,OLED_CMD);// Disable Inverse Display On (0xa6/a7)
  OLED_WR_Byte(0xAF,OLED_CMD);
  OLED_Clear();
}
