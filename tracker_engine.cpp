#include <iostream>
#include <vector>
#include <cmath>
#include <iomanip>
#include <map>
#include <algorithm>
#include <random>
#include <chrono>
#include <ctime>
#include <sstream>
#include <sys/stat.h>
#include <opencv2/opencv.hpp>

const int GRID_W = 40;
const int GRID_H = 20;
const int TOTAL_FRAMES = 20;

struct PlumeSource {
    std::string gas_type;
    double base_x, base_y;
    double intensity;
    double base_vx, base_vy;
    bool is_stationary;
};

// Generates simulation grid frame with fixed persistent cloud assignments
std::vector<std::vector<double>> renderGridFrame(int t, const std::vector<PlumeSource>& sources) {
    std::vector<std::vector<double>> grid(GRID_H, std::vector<double>(GRID_W, 0.0));
    
    for (const auto& src : sources) {
        int active_duration = t;
        
        // If stationary, position doesn't change over time; if moving, apply velocity & subtle drift
        double curr_x = src.base_x;
        double curr_y = src.base_y;
        
        if (!src.is_stationary) {
            double turbulence_x = std::sin(active_duration * 0.5 + src.base_x) * 0.2;
            double turbulence_y = std::cos(active_duration * 0.4 + src.base_y) * 0.15;
            curr_x += (src.base_vx + turbulence_x) * active_duration;
            curr_y += (src.base_vy + turbulence_y) * active_duration;
        }

        for (int y = 0; y < GRID_H; ++y) {
            for (int x = 0; x < GRID_W; ++x) {
                double dist_sq = (x - curr_x) * (x - curr_x) + (y - curr_y) * (y - curr_y);
                double plume_val = src.intensity * std::exp(-dist_sq / 7.0);
                grid[y][x] += plume_val;
            }
        }
    }
    return grid;
}

// Helper to get formatted timestamps for folders and display
std::string getTimestampString(const std::string& format_str) {
    auto now = std::chrono::system_clock::now();
    std::time_t now_time = std::chrono::system_clock::to_time_t(now);
    char buf[100];
    std::strftime(buf, sizeof(buf), format_str.c_str(), std::localtime(&now_time));
    return std::string(buf);
}

int main() {
    std::cout << "====================================================\n";
    std::cout << "=== FTIR PLUME CONSISTENT SIMULATION ENGINE      ===\n";
    std::cout << "====================================================\n\n";

    std::string display_time = getTimestampString("%Y-%m-%d %H:%M");
    std::string folder_time = getTimestampString("%Y%m%d_%H%M%S");
    std::string run_directory = "run_" + folder_time;

// Create unique output directory for this run
#if defined(_WIN32)
    ::_mkdir(run_directory.c_str());
#else
    ::mkdir(run_directory.c_str(), 0777);
#endif
    std::cout << "[INFO] Created Unique Run Directory: " << run_directory << "/\n";

    // Load background image
    cv::Mat background = cv::imread("../gas_refinery.png");
    if (background.empty()) {
        std::cerr << "[ERROR] Could not load gas_refinery.png! Ensure it is in the root directory.\n";
        return 1;
    }

    // Setup Video Writer inside the unique run folder
    std::string video_filename = run_directory + "/ftir_simulation_output.mp4";
    int codec = cv::VideoWriter::fourcc('a', 'v', 'c', '1');
    double fps = 2.0;
    cv::VideoWriter video_writer(video_filename, codec, fps, background.size(), true);

    // Randomly generate 1 to 5 persistent clouds fixed for this entire run
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> cloud_count_dist(0, 5); 
    std::uniform_real_distribution<> pos_x_dist(6.0, 34.0);
    std::uniform_real_distribution<> pos_y_dist(4.0, 16.0);
    std::uniform_real_distribution<> intensity_dist(0.8, 0.98);
    std::uniform_real_distribution<> vel_dist(-0.4, 0.4);
    std::bernoulli_distribution stationary_dist(0.4); // 40% chance a cloud is stationary

    std::vector<std::string> possible_gases = {"Ammonia", "Methane", "Chlorine", "Propane", "HydrogenSulfide"};
    std::uniform_int_distribution<> gas_type_dist(0, possible_gases.size() - 1);

    int generated_clouds = cloud_count_dist(gen);
    std::vector<PlumeSource> active_leaks;

    std::cout << "[SCENARIO] Locked " << generated_clouds << " consistent cloud(s) for this run session:\n";
    for (int i = 0; i < generated_clouds; ++i) {
        PlumeSource src;
        src.gas_type = possible_gases[gas_type_dist(gen)];
        src.base_x = pos_x_dist(gen);
        src.base_y = pos_y_dist(gen);
        src.intensity = intensity_dist(gen);
        src.base_vx = vel_dist(gen);
        src.base_vy = vel_dist(gen);
        src.is_stationary = stationary_dist(gen);
        active_leaks.push_back(src);
        
        std::cout << "  - Plume #" << (i + 1) << ": Type=" << src.gas_type 
                  << " | Pos(" << std::fixed << std::setprecision(1) << src.base_x << ", " << src.base_y 
                  << ") | Mode: " << (src.is_stationary ? "Stationary" : "Moving") << "\n";
    }
    std::cout << "\n";

    // Iterative Frame Processing Loop
    for (int t = 0; t < TOTAL_FRAMES; ++t) {
        std::cout << "----------------------------------------------------\n";
        std::cout << "[PROCESSING] Frame t = " << t << " / " << (TOTAL_FRAMES - 1) << "...\n";

        auto grid = renderGridFrame(t, active_leaks);
        cv::Mat frame = background.clone();
        cv::Mat heat_mask = cv::Mat::zeros(background.size(), CV_8UC3);

        int anomalous_pixels = 0;
        for (int gy = 0; gy < GRID_H; ++gy) {
            for (int gx = 0; gx < GRID_W; ++gx) {
                double val = grid[gy][gx];
                if (val > 0.15) {
                    anomalous_pixels++;
                    int img_x = (gx * background.cols) / GRID_W;
                    int img_y = (gy * background.rows) / GRID_H;
                    int box_w = background.cols / GRID_W;
                    int box_h = background.rows / GRID_H;

                    uchar intensity_byte = static_cast<uchar>(std::min(val * 255.0, 255.0));
                    cv::rectangle(heat_mask, cv::Rect(img_x, img_y, box_w, box_h), cv::Scalar(0, intensity_byte, 255 - intensity_byte), -1);
                }
            }
        }

        cv::Mat colored_heat;
        cv::applyColorMap(heat_mask, colored_heat, cv::COLORMAP_JET);

        double alpha = 0.45;
        cv::Mat blended;
        cv::addWeighted(colored_heat, alpha, frame, 1.0 - alpha, 0.0, blended);

        // Draw targets based on persistent cloud states
        for (const auto& src : active_leaks) {
            double cx = src.base_x;
            double cy = src.base_y;
            if (!src.is_stationary) {
                double turb_x = std::sin(t * 0.5 + src.base_x) * 0.2;
                double turb_y = std::cos(t * 0.4 + src.base_y) * 0.15;
                cx += (src.base_vx + turb_x) * t;
                cy += (src.base_vy + turb_y) * t;
            }

            int px = static_cast<int>((cx / GRID_W) * background.cols);
            int py = static_cast<int>((cy / GRID_H) * background.rows);

            cv::rectangle(blended, cv::Rect(px - 65, py - 45, 130, 90), cv::Scalar(0, 255, 0), 2);
            cv::putText(blended, src.gas_type + " [LATCHED]", cv::Point(px - 63, py - 53), 
                        cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(0, 255, 0), 2);

            std::cout << "  -> TRACKED TARGET [" << src.gas_type << "]: Pos(" << std::fixed << std::setprecision(1) 
                      << cx << ", " << cy << ") | Status: " << (src.is_stationary ? "Stationary" : "Drifting") << "\n";
        }

        // Stamp Timestamp onto the bottom-left corner
        std::string timestamp_label = "RUN TIME: " + display_time + " | FRAME: " + std::to_string(t);
        cv::putText(blended, timestamp_label, cv::Point(30, background.rows - 30),
                    cv::FONT_HERSHEY_SIMPLEX, 0.7, cv::Scalar(255, 255, 255), 2);

        // Save individual frame into the unique run folder
        std::string filename = run_directory + "/output_frame_t" + std::to_string(t) + ".png";
        cv::imwrite(filename, blended);
        if (video_writer.isOpened()) {
            video_writer.write(blended);
        }
        std::cout << "  [SUCCESS] Frame saved: " << filename << "\n";
    }

    if (video_writer.isOpened()) {
        video_writer.release();
        std::cout << "\n[SUCCESS] MP4 Video saved to: " << video_filename << "\n";
    }

    std::cout << "================================================----\n";
    return 0;
}