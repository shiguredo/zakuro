#ifndef GAME_BREAKOUT_H_
#define GAME_BREAKOUT_H_

#include <math.h>

#include <algorithm>
#include <chrono>
#include <vector>

// Blend2D
#include <blend2d.h>

#include "embedded_binary.h"
#include "game_audio.h"
#include "game_key.h"

class GameKuzushi {
 public:
  GameKuzushi(int width, int height, GameAudioManager* gam) : gam_(gam) {
    Init(width, height);
  }

  void Render(BLContext& ctx,
              std::chrono::high_resolution_clock::time_point now) {
    // 初回
    if (prev_.time_since_epoch().count() == 0) {
      prev_ = now;
    }

    auto ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(now - prev_);
    for (int i = 0; i < ms.count(); i++) {
      Update();
    }
    prev_ = now;
    Draw(ctx);
  }

 private:
  void Init(int width, int height) {
    key_.Init();

    width_ = width;
    height_ = height;

    const int WORDS = 6;
    const int CHARAS_IN_WORD = 4;
    const int PADDLE_CHARAS = 4;
    chara_width_ = 1.0 / (WORDS * CHARAS_IN_WORD);
    word_width_ = chara_width_ * CHARAS_IN_WORD;
    paddle_.x =
        (int)((WORDS * CHARAS_IN_WORD - PADDLE_CHARAS) / 2) * chara_width_;
    paddle_.y = 0.90;
    paddle_.w = chara_width_ * PADDLE_CHARAS;
    paddle_.h = 0.03;

    InitBall(-44.7 * M_PI / 180);
    init_ball_speed_ = 0.0005;

    const int BLOCKS_IN_ROW = 6;
    const int BLOCK_ROWS = 6;
    double block_w = 1.0 / (BLOCKS_IN_ROW + 1);
    double block_x = block_w / 2;
    double block_h = 0.5 / (BLOCK_ROWS + 1);
    double block_y = block_h;
    for (int y = 0; y < BLOCK_ROWS; y++) {
      for (int x = 0; x < BLOCKS_IN_ROW; x++) {
        Block block;
        block.enabled = true;
        block.x = block_x + x * block_w;
        block.y = block_y + y * block_h;
        block.w = block_w;
        block.h = block_h;

        BLRgba32 rgb;
        const double D = 10;
        srand(time(nullptr));
        while (true) {
          rgb = BLRgba32(rand() % 255, rand() % 255, rand() % 255);
          //std::cout << "x=" << x << ", y=" << y << std::endl;
          //std::cout << "RGB(" << rgb.r << "," << rgb.g << "," << rgb.b << ")"
          //          << std::endl;
          if (Diff(rgb, BLRgba32(255, 255, 255)) < D) {
            continue;
          }
          if (Diff(rgb, BLRgba32(0, 0, 0)) < D) {
            continue;
          }
          if (Diff(rgb, BLRgba32(128, 128, 128)) < D) {
            continue;
          }
          bool found = false;
          for (const auto& b : blocks_) {
            if (Diff(rgb, b.rgb) < D) {
              found = true;
              break;
            }
          }
          if (found) {
            continue;
          }
          break;
        }
        block.rgb = rgb;

        blocks_.push_back(block);
        std::cout << blocks_.size() << " / " << (BLOCKS_IN_ROW * BLOCK_ROWS)
                  << " done" << std::endl;
      }
    }

    {
      BLFontFace face;
      BLFontData data;
      auto content = EmbeddedBinary::kosugi_ttf();
      data.createFromData((const uint8_t*)content.ptr, content.size);
      face.createFromData(data, 0);
      font_.createFromFace(face, height_ * 0.03);
    }
  }
  void InitBall(double angle) {
    ball_.x = 0.5;
    ball_.y = 0.80;
    ball_.rx = 0.01;
    ball_.ry = 0.01 * width_ / height_;
    ball_.speed = 0;
    ball_.angle = angle;
  }
  // 1ミリ秒分進める
  void Update() {
    double aspect = width_ / height_;
    while (true) {
      int c = key_.PopKey();
      if (c < 0) {
        break;
      }

      // パドルの移動
      if (c == 'h') {
        paddle_.x -= chara_width_;
      }
      if (c == 'l') {
        paddle_.x += chara_width_;
      }
      if (c == 'w') {
        double x = 0;
        while (x <= paddle_.x + 0.0001) {
          x += word_width_;
        }
        paddle_.x = x;
      }
      if (c == 'b') {
        double x = 1.0 - word_width_;
        while (x >= paddle_.x - 0.0001) {
          x -= word_width_;
        }
        paddle_.x = x;
      }
      if (c == '0') {
        paddle_.x = 0;
      }
      if (c == '$') {
        paddle_.x = 1.0 - paddle_.w;
      }

      if (paddle_.x < 0) {
        paddle_.x = 0;
      }
      if (paddle_.x + paddle_.w > 1.0) {
        paddle_.x = 1.0 - paddle_.w;
      }

      // ボールの移動開始
      if (!next_stage_ && ball_.speed == 0 && c == ' ') {
        ball_.speed = init_ball_speed_;
        view_help_ = false;
      }
    }

    if (next_stage_) {
      UpdateNextStage();
      return;
    }

    // ボールの移動
    {
      ball_.x += ball_.speed * cos(ball_.angle);
      ball_.y += ball_.speed * sin(ball_.angle) * aspect;
      // 左右に当たる
      if (ball_.x - ball_.rx < 0 || ball_.x + ball_.rx > 1.0) {
        double ra = M_PI / 2;
        double tx = cos(ball_.angle - ra);
        double ty = sin(ball_.angle - ra);
        ball_.angle = atan2(-ty, tx) + ra;
        if (ball_.x - ball_.rx < 0) {
          ball_.x = ball_.rx;
        } else {
          ball_.x = 1.0 - ball_.rx;
        }
      }
      // 上に当たる
      if (ball_.y - ball_.ry < 0) {
        double ra = 0;
        double tx = cos(ball_.angle - ra);
        double ty = sin(ball_.angle - ra);
        ball_.angle = atan2(-ty, tx) + ra;
        ball_.y = ball_.ry;
      }

      // ブロックに当たる
      for (int i = 0; i < blocks_.size(); i++) {
        auto& block = blocks_[i];
        if (!block.enabled) {
          continue;
        }
        if (ball_.x + ball_.rx > block.x &&
            ball_.x - ball_.rx < block.x + block.w &&
            ball_.y + ball_.ry > block.y &&
            ball_.y - ball_.ry < block.y + block.h) {
          block.enabled = false;
          broken_block_indices_.push_back(i);
          {
            BGLayer layer;
            layer.t = 0.0;
            layer.rgb = block.rgb;
            layer.sx1 = block.x;
            layer.sy1 = block.y;
            layer.sx2 = block.x + block.w;
            layer.sy2 = block.y + block.h;
            layer.tx1 = 0.0;
            layer.ty1 = 0.0;
            layer.tx2 = 1.0;
            layer.ty2 = 1.0;
            layers_.push_back(layer);
          }
          const double freqs[] = {
              1046.502,  // C6
              //1108.731,    // C#6
              1174.659,  // D6
              //1244.508,    // D#6
              1318.510,  // E6
              1396.913,  // F6
              //1479.978,    // F#6
              1567.982,  // G6
              //1661.219,    // G#6
              1760.000,  // A6
              //1864.655,    // A#6
              1975.533,  // B6
              2093.005,  // C7
          };
          if (combo_ >= sizeof(freqs) / sizeof(freqs[0])) {
            combo_ = sizeof(freqs) / sizeof(freqs[0]) - 1;
          }
          gam_->PlayAny(freqs[combo_], 0.02, 0.2);
          combo_ += 1;

          double w1 = ball_.x + ball_.rx - block.x;
          double w2 = block.x + block.w - (ball_.x - ball_.rx);
          double h1 = ball_.y + ball_.ry - block.y;
          double h2 = block.y + block.h - (ball_.y - ball_.ry);
          double w = std::min(w1, w2);
          double h = std::min(h1, h2);
          if (w > h) {
            // 横軸に反射
            double ra = 0;
            double tx = cos(ball_.angle - ra);
            double ty = sin(ball_.angle - ra);
            ball_.angle = atan2(-ty, tx) + ra;
            if (h1 < h2) {
              // 上から当たってる
              ball_.y = block.y - ball_.ry;
            } else {
              // 下から当たってる
              ball_.y = block.y + block.h + ball_.ry;
            }
          } else {
            // 縦軸に反射
            double ra = M_PI / 2;
            double tx = cos(ball_.angle - ra);
            double ty = sin(ball_.angle - ra);
            ball_.angle = atan2(-ty, tx) + ra;
            if (w1 < w2) {
              // 左から当たってる
              ball_.x = block.x - ball_.rx;
            } else {
              // 右から当たってる
              ball_.x = block.x + block.w + ball_.rx;
            }
          }
        }
      }

      // パドルに当たる
      if (ball_.x + ball_.rx > paddle_.x &&
          ball_.x - ball_.rx < paddle_.x + paddle_.w &&
          ball_.y + ball_.ry > paddle_.y &&
          ball_.y - ball_.ry < paddle_.y + paddle_.h) {
        double ra = 0;
        double tx = cos(ball_.angle - ra);
        double ty = sin(ball_.angle - ra);
        ball_.angle = atan2(-ty, tx) + ra;
        ball_.y = paddle_.y - ball_.ry;
        gam_->PlayAny(220, 0.04, 0.1);
      }

      // 落ちる
      if (ball_.y - ball_.ry > 1.0) {
        double ra = 0;
        double tx = cos(ball_.angle - ra);
        double ty = sin(ball_.angle - ra);
        double angle = atan2(-ty, tx) + ra;
        InitBall(angle);
      }
    }

    // 背景
    {
      for (auto& layer : layers_) {
        layer.t += 0.002;
      }

      if (layers_.size() >= 2) {
        // ２番目のレイヤーが１番目のレイヤーを完全に覆ったら、１番目のレイヤーを削除する
        if (layers_[0].t >= 1.0 && layers_[1].t >= 1.0) {
          layers_.erase(layers_.begin());
        }
      }

      // 動かすレイヤーが無くなったら黒の背景に戻す
      if (layers_.size() == 1 && layers_[0].rgb != BLRgba32(0, 0, 0) &&
          layers_[0].t >= 1.0) {
        BGLayer layer;
        layer.t = 0.0;
        layer.rgb = BLRgba32(0, 0, 0);
        layer.sx1 = 0.5;
        layer.sy1 = 0.5;
        layer.sx2 = 0.5;
        layer.sy2 = 0.5;
        layer.tx1 = 0.0;
        layer.ty1 = 0.0;
        layer.tx2 = 1.0;
        layer.ty2 = 1.0;
        layers_.push_back(layer);

        combo_ = 0;
      }
    }

    // 終了確認
    bool end = true;
    for (const auto& block : blocks_) {
      if (block.enabled) {
        end = false;
        break;
      }
    }
    if (end) {
      ball_.speed = 0.0;
      if (layers_.size() == 1 && layers_[0].rgb == BLRgba32(0, 0, 0) &&
          layers_[0].t >= 1.0) {
        next_stage_ = true;
        auto black_layer = layers_[0];
        layers_.clear();
        for (int index : broken_block_indices_) {
          const auto& block = blocks_[index];
          BGLayer layer;
          layer.t = 1.0;
          layer.rgb = block.rgb;
          layer.sx1 = block.x;
          layer.sy1 = block.y;
          layer.sx2 = block.x + block.w;
          layer.sy2 = block.y + block.h;
          layer.tx1 = 0.0;
          layer.ty1 = 0.0;
          layer.tx2 = 1.0;
          layer.ty2 = 1.0;
          layers_.push_back(layer);
        }
        layers_.push_back(black_layer);
      }
    }
  }
  void UpdateNextStage() {
    if (!layers_.empty()) {
      auto& layer = layers_.back();
      layer.t -= 0.005;
      if (layer.t < 0.0) {
        if (layer.rgb != BLRgba32(0, 0, 0)) {
          blocks_[broken_block_indices_.back()].enabled = true;
          broken_block_indices_.pop_back();
        }
        layers_.pop_back();
      }
    }
    if (layers_.empty()) {
      double angle = ball_.angle;
      if (sin(ball_.angle) > 0.0) {
        double ra = 0;
        double tx = cos(ball_.angle - ra);
        double ty = sin(ball_.angle - ra);
        angle = atan2(-ty, tx) + ra;
      }

      InitBall(angle);
      init_ball_speed_ *= 1.2;
      next_stage_ = false;
    }
  }

  void Draw(BLContext& ctx) {
    ctx.setCompOp(BL_COMP_OP_SRC_COPY);
    ctx.setFillStyle(BLRgba32(0, 0, 0));
    ctx.fillAll();

    ctx.scale(width_, height_);

    auto fill_layer = [&ctx](const BGLayer& layer) {
      double x1, y1, x2, y2;
      layer.Calc(x1, y1, x2, y2);
      ctx.setFillStyle(layer.rgb);
      ctx.fillRect(x1, y1, x2 - x1, y2 - y1);
    };
    if (next_stage_) {
      // 後ろの2枚だけ表示する
      if (layers_.size() >= 2) {
        fill_layer(layers_[layers_.size() - 2]);
      }
      if (layers_.size() >= 1) {
        fill_layer(layers_[layers_.size() - 1]);
      }
    } else {
      for (const auto& layer : layers_) {
        fill_layer(layer);
      }
    }

    if (view_help_) {
      ctx.resetMatrix();
      ctx.setStrokeStyle(BLRgba32(255, 255, 255));
      ctx.setFillStyle(BLRgba32(255, 255, 255));
      std::string text =
          "h:左 l:右 b:大右 w:大左 0:左端 $:右端 Space:ゲーム開始";
      ctx.fillUtf8Text(BLPoint(width_ * 0.05, height_ * 0.05), font_,
                       text.c_str());
      ctx.scale(width_, height_);
    }

    ctx.setFillStyle(BLRgba32(255, 255, 255));

    // ガイド
    ctx.setStrokeWidth(2 / width_);
    {
      ctx.setStrokeStyle(BLRgba32(128, 128, 128));
      double x = 0;
      while (x < 1.0) {
        ctx.strokeLine(x, paddle_.y, x, paddle_.y + paddle_.h / 2);
        x += chara_width_;
      }
    }
    {
      ctx.setStrokeStyle(BLRgba32(255, 255, 255));
      double x = 0;
      while (x < 1.0) {
        ctx.strokeLine(x, paddle_.y, x, paddle_.y + paddle_.h);
        x += word_width_;
      }
    }

    // パドル
    ctx.fillRect(paddle_.x, paddle_.y, paddle_.w, paddle_.h);

    // ボール
    ctx.fillEllipse(ball_.x, ball_.y, ball_.rx, ball_.ry);

    // ブロック
    for (const auto& block : blocks_) {
      if (!block.enabled) {
        continue;
      }
      ctx.setFillStyle(block.rgb);
      ctx.fillRect(block.x, block.y, block.w, block.h);
    }
  }

 private:
  struct Lab {
    double L;
    double a;
    double b;
  };
  static Lab ToCIELAB(BLRgba32 rgb) {
    double r = (double)rgb.r / 255;
    double g = (double)rgb.g / 255;
    double b = (double)rgb.b / 255;
    double x = 0.4124 * r + 0.3576 * g + 0.1805 * b;
    double y = 0.2126 * r + 0.7152 * g + 0.0722 * b;
    double z = 0.0193 * r + 0.1192 * g + 0.9505 * b;
    double xn = 0.4124 + 0.3576 + 0.1805;
    double yn = 0.2126 + 0.7152 + 0.0722;
    double zn = 0.0193 + 0.1192 + 0.9505;
    auto f = [](double t) {
      return t > pow(6.0 / 29.0, 3)
                 ? pow(t, 1.0 / 3.0)
                 : (t / (3 * pow(6.0 / 29.0, 2)) + 4.0 / 29.0);
    };
    Lab lab;
    lab.L = 116.0 * f(y / yn) - 16.0;
    lab.a = 500.0 * (f(x / xn) - f(y / yn));
    lab.b = 200.0 * (f(y / yn) - f(z / zn));
    return lab;
  }
  static double Diff(BLRgba32 rgb1, BLRgba32 rgb2) {
    Lab p = ToCIELAB(rgb1);
    Lab q = ToCIELAB(rgb2);

    // https://github.com/yuki-koyama/color-util/blob/master/include/color-util/CIEDE2000.hpp より
    constexpr double epsilon = 1e-10;

    auto my_atan = [](double y, double x) {
      const double value = std::atan2(y, x) * 180.0 / M_PI;
      return (value < 0.0) ? value + 360.0 : value;
    };
    auto my_sin = [](double x) { return std::sin(x * M_PI / 180.0); };
    auto my_cos = [](double x) { return std::cos(x * M_PI / 180.0); };
    auto get_h_prime = [my_atan](double a_prime, double b) {
      const bool a_prime_and_b_are_zeros =
          (std::abs(a_prime) < epsilon) && (std::abs(b) < epsilon);
      return a_prime_and_b_are_zeros ? 0.0 : my_atan(b, a_prime);
    };
    auto get_delta_h_prime = [](double C_1_prime, double C_2_prime,
                                double h_1_prime, double h_2_prime) {
      if (C_1_prime * C_2_prime < epsilon) {
        return 0.0;
      }

      const double diff = h_2_prime - h_1_prime;

      if (std::abs(diff) <= 180.0) {
        return diff;
      } else if (diff > 180.0) {
        return diff - 360.0;
      } else {
        return diff + 360.0;
      }
    };
    auto get_h_prime_bar = [](double C_1_prime, double C_2_prime,
                              double h_1_prime, double h_2_prime) {
      if (C_1_prime * C_2_prime < epsilon) {
        return h_1_prime + h_2_prime;
      }

      const double dist = std::abs(h_1_prime - h_2_prime);
      const double sum = h_1_prime + h_2_prime;

      if (dist <= 180.0) {
        return 0.5 * sum;
      } else if (sum < 360.0) {
        return 0.5 * (sum + 360.0);
      } else {
        return 0.5 * (sum - 360.0);
      }
    };

    double L_1 = p.L;
    double a_1 = p.a;
    double b_1 = p.b;
    double L_2 = q.L;
    double a_2 = q.a;
    double b_2 = q.b;

    // Step 1

    const double C_1_ab = std::sqrt(a_1 * a_1 + b_1 * b_1);
    const double C_2_ab = std::sqrt(a_2 * a_2 + b_2 * b_2);
    const double C_ab_bar = 0.5 * (C_1_ab + C_2_ab);
    const double G =
        0.5 *
        (1.0 - std::sqrt(std::pow(C_ab_bar, 7.0) /
                         (std::pow(C_ab_bar, 7.0) + std::pow(25.0, 7.0))));
    const double a_1_prime = (1.0 + G) * a_1;
    const double a_2_prime = (1.0 + G) * a_2;
    const double C_1_prime = std::sqrt(a_1_prime * a_1_prime + b_1 * b_1);
    const double C_2_prime = std::sqrt(a_2_prime * a_2_prime + b_2 * b_2);
    const double h_1_prime = get_h_prime(a_1_prime, b_1);
    const double h_2_prime = get_h_prime(a_2_prime, b_2);

    // Step 2

    const double delta_L_prime = L_2 - L_1;
    const double delta_C_prime = C_2_prime - C_1_prime;
    const double delta_h_prime =
        get_delta_h_prime(C_1_prime, C_2_prime, h_1_prime, h_2_prime);
    const double delta_H_prime =
        2.0 * std::sqrt(C_1_prime * C_2_prime) * my_sin(0.5 * delta_h_prime);

    // Step 3

    const double L_prime_bar = 0.5 * (L_1 + L_2);
    const double C_prime_bar = 0.5 * (C_1_prime + C_2_prime);
    const double h_prime_bar =
        get_h_prime_bar(C_1_prime, C_2_prime, h_1_prime, h_2_prime);

    const double T = 1.0 - 0.17 * my_cos(h_prime_bar - 30.0) +
                     0.24 * my_cos(2.0 * h_prime_bar) +
                     0.32 * my_cos(3.0 * h_prime_bar + 6.0) -
                     0.20 * my_cos(4.0 * h_prime_bar - 63.0);

    const double delta_theta = 30.0 * std::exp(-((h_prime_bar - 275) / 25.0) *
                                               ((h_prime_bar - 275) / 25.0));

    const double R_C =
        2.0 * std::sqrt(std::pow(C_prime_bar, 7.0) /
                        (std::pow(C_prime_bar, 7.0) + std::pow(25.0, 7.0)));
    const double S_L =
        1.0 + (0.015 * (L_prime_bar - 50.0) * (L_prime_bar - 50.0)) /
                  std::sqrt(20.0 + (L_prime_bar - 50.0) * (L_prime_bar - 50.0));
    const double S_C = 1.0 + 0.045 * C_prime_bar;
    const double S_H = 1.0 + 0.015 * C_prime_bar * T;
    const double R_T = -my_sin(2.0 * delta_theta) * R_C;

    constexpr double k_L = 1.0;
    constexpr double k_C = 1.0;
    constexpr double k_H = 1.0;

    const double delta_L = delta_L_prime / (k_L * S_L);
    const double delta_C = delta_C_prime / (k_C * S_C);
    const double delta_H = delta_H_prime / (k_H * S_H);

    const double delta_E_squared = delta_L * delta_L + delta_C * delta_C +
                                   delta_H * delta_H + R_T * delta_C * delta_H;

    double r = std::sqrt(delta_E_squared);
    return r;
  }
  static double Bezier(double x, double x1, double y1, double x2, double y2) {
    if (x <= 0.0) {
      return 0.0;
    }
    if (x >= 1.0) {
      return 1.0;
    }

    // ±eps の範囲内に収まったら抜ける
    static const double eps = 0.01;

    double down = 0.0f;
    double up = 1.0f;

    double t;
    while (true) {
      t = (down + up) / 2;
      double px =
          3 * (1 - t) * (1 - t) * t * x1 + 3 * (1 - t) * t * t * x2 + t * t * t;
      if (px < x - eps) {
        down = t;
      } else if (px > x + eps) {
        up = t;
      } else {
        break;
      }
    }

    return 3 * (1 - t) * (1 - t) * t * y1 + 3 * (1 - t) * t * t * y2 +
           t * t * t;
  }

 private:
  double width_;
  double height_;

  bool next_stage_ = false;
  bool view_help_ = true;

  struct Paddle {
    double x;
    double y;
    double w;
    double h;
  };
  Paddle paddle_;
  double chara_width_;
  double word_width_;

  struct Ball {
    double x;
    double y;
    double rx;
    double ry;
    double speed;
    double angle;
  };
  Ball ball_;
  double init_ball_speed_;

  struct Block {
    bool enabled;
    BLRgba32 rgb;
    double x;
    double y;
    double w;
    double h;
  };
  std::vector<Block> blocks_;
  std::vector<int> broken_block_indices_;
  int combo_ = 0;

  struct BGLayer {
    double t;
    BLRgba32 rgb;

    double sx1;
    double sy1;
    double sx2;
    double sy2;

    double tx1;
    double ty1;
    double tx2;
    double ty2;
    // t と各座標から x1,y1,x2,y2 を計算する
    void Calc(double& x1, double& y1, double& x2, double& y2) const {
      auto easein = [](double t) {
        return Bezier(t, 0.420, 0.000, 1.000, 1.000);
      };

      double t = easein(this->t);
      x1 = (tx1 - sx1) * t + sx1;
      y1 = (ty1 - sy1) * t + sy1;
      x2 = (tx2 - sx2) * t + sx2;
      y2 = (ty2 - sy2) * t + sy2;
    }
  };
  std::vector<BGLayer> layers_;

  std::chrono::high_resolution_clock::time_point prev_;
  GameKey key_;
  GameAudioManager* gam_;
  BLFont font_;
};

#endif
