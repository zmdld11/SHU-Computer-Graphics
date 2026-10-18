// ImageIO.cpp
// 实现 PPM(P6) 导出：P6 头 + RGB 字节流，Windows 照片打不开，但可用任何转换工具转 PNG。
#include "core/ImageIO.hpp"

#include <fstream>
#include <iostream>

namespace cg {

bool writePPM(const char* path, const FrameBuffer& fb) {
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        std::cerr << "[io] 无法写入 " << path << std::endl;
        return false;
    }
    out << "P6\n" << fb.width() << " " << fb.height() << "\n255\n";
    for (int y = 0; y < fb.height(); ++y) {
        for (int x = 0; x < fb.width(); ++x) {
            const Color c = fb.pixel(x, y);
            out.put(static_cast<char>(c.r));
            out.put(static_cast<char>(c.g));
            out.put(static_cast<char>(c.b));
        }
    }
    return out.good();
}

} // namespace cg
