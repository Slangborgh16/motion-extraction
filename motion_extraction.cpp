#include <queue>                    // Used for the frame buffer
#include <string>                   // Standard string operations             
#include <cstdlib>                  // std::exit()
#include <iostream>                 // Standard IO operations
#include <getopt.h>                 // Parse arguments with getopt_long
#include <unistd.h>                 // Parse arguments with getopt

#include <opencv2/videoio.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/core/types.hpp>
#include <opencv2/core/utility.hpp>

unsigned char gammaLUT[256];        // Lookup table to quickly apply gamma correction to frames

struct arguments {                  // parseArgs() returns this struct
    std::string inputPath;
    std::string outputPath;
    int framesToSkip = 0;
    int secondsToSkip = 0;
    bool framesOption = false;
    bool secondsOption = false;
    bool overlay = false;
};

arguments parseArgs(int argc, char* argv[]);
void createGammaLUT(unsigned char lut[256], float gamma_);
void applyGammaCorrection(cv::Mat& image);
void compareFrames(const cv::Mat& src1, const cv::Mat& src2, cv::Mat& dst);
void extractMotion(cv::VideoCapture& inputVideo, cv::VideoWriter& outputVideo, unsigned long frameDelay, bool overlay);

int main(int argc, char* argv[]) {
    createGammaLUT(gammaLUT, 1/1.1);    // Fill the lookup table with precomputed gamma values

    unsigned long frameDelay;

    arguments args = parseArgs(argc, argv);

    cv::VideoCapture inputVideo(args.inputPath);

    if (!inputVideo.isOpened()) {
        std::cerr << "Error: Could not open file " << args.outputPath << std::endl;
        std::exit(EXIT_FAILURE);
    }

    // Stats about the input video for creating the output video stream
    int videoWidth = static_cast<int>(inputVideo.get(cv::CAP_PROP_FRAME_WIDTH));
    int videoHeight = static_cast<int>(inputVideo.get(cv::CAP_PROP_FRAME_HEIGHT));
    double fps = inputVideo.get(cv::CAP_PROP_FPS);
    double frameCount = inputVideo.get(cv::CAP_PROP_FRAME_COUNT);
    int fourcc = cv::VideoWriter::fourcc('a', 'v', 'c', '1');

    // Ensure that the frame delay from the command line args is less than the length of the video
    if (args.framesOption) {
        frameDelay = args.framesToSkip;
        if (frameDelay > frameCount) {
            std::cerr << "Error: Input video only has " << static_cast<int>(frameCount) << " frame(s). Cannot offset by " \
            << frameDelay << " frame(s)." << std::endl;
            std::exit(EXIT_FAILURE);
        }
    }

    if (args.secondsOption) {
        frameDelay = args.secondsToSkip * fps;
        if (frameDelay > frameCount) {
            std::cerr << "Error: Input video is only " << static_cast<int>(frameCount / fps) << " second(s) long. Cannot offset by " \
            << args.secondsToSkip << " second(s)." << std::endl;
            std::exit(EXIT_FAILURE);
        }
    }

    cv::VideoWriter outputVideo(args.outputPath, fourcc, fps, cv::Size(videoWidth, videoHeight));
    
    if (!outputVideo.isOpened()) {
        std::cerr << "Error: Could not create the output video file " << args.outputPath << std::endl;
        std::exit(EXIT_FAILURE);
    }
    
    extractMotion(inputVideo, outputVideo, frameDelay, args.overlay);

    return 0;
}


arguments parseArgs(int argc, char* argv[]) {
    auto printUsage = [](const std::string& programName) {
        std::cout << "Usage: " << programName << " input_path output_path [-f frames | -s seconds] [-o] [-h]" \
        << std::endl;
    };

    auto printHelp = [&printUsage](const std::string& programName) {
        printUsage(programName);
        std::cout << "\nArguments:" << std::endl;
        std::cout << "  input_path         Path to input video file (MP4)" << std::endl;
        std::cout << "  output_path        Path of output video file to save (MP4)" << std::endl;
        std::cout << "\nOptions:" << std::endl;
        std::cout << "  -f, --frames       Number of frames to offset by" << std::endl;
        std::cout << "  -s, --seconds      Number of seconds to offset by" << std::endl;
        std::cout << "  -o, --overlay      Overlay the extracted motion over the original video" << std::endl;
        std::cout << "  -h, --help         Display this help message" << std::endl;
        std::cout << "\nNOTE: --frames and --seconds are mutually exclusive." << std::endl;
        std::cout << "A small offset shows fast movements in the video. A large offset shows slow movements in the video." << std::endl;
        std::cout << "If -f or -s is set to 0, the output video shows change from the start of the video." << std::endl;
        std::cout << "\nExample:" << std::endl;
        std::cout << "  " << programName << " input.mp4 output.mp4 -s 1" << std::endl;
    };

    arguments args;
    std::string programName = argv[0];
    int opt;

    // For getopt_long() -- allows for the long and short name of the option to be given (ex. -o vs. --overlay)
    const char* const short_opts = "f:s:oh";
    const option long_opts[] = {
        {"frames",  required_argument, nullptr, 'f'},
        {"seconds", required_argument, nullptr, 's'},
        {"overlay", no_argument,       nullptr, 'o'},
        {"help",    no_argument,       nullptr, 'h'},
        {nullptr,   no_argument,       nullptr, 0}
    };
    
    // Set variables depending on command line args
    while ((opt = getopt_long(argc, argv, short_opts, long_opts, nullptr)) != -1) {
        switch (opt) {
            case 'h':
                printHelp(programName);
                std::exit(EXIT_SUCCESS);
            case 'f':
                args.framesToSkip = std::stoi(optarg);
                args.framesOption = true;
                break;
            case 's':
                args.secondsToSkip = std::stoi(optarg);
                args.secondsOption = true;
                break;
            case 'o':
                args.overlay = true;
                break;
            case '?':
                std::cerr << "Unknown option: " << static_cast<char>(optopt) << std::endl;
                printUsage(programName);
                std::exit(EXIT_FAILURE);
            default:
                break;
        }
    }

    // The user may not provide both --frames and --seconds
    if (args.framesOption && args.secondsOption) {
        std::cerr << "Error: Options -f and -s are mutually exclusive." << std::endl;
        printUsage(programName);
        std::exit(EXIT_FAILURE);
    }

    // Either --frames or --seconds must be provided
    if (!args.framesOption && !args.secondsOption) {
        std::cerr << "Error: You must provide either a seconds or frames offset with -s or -f." << std::endl;
        printUsage(programName);
        std::exit(EXIT_FAILURE);
    }

    if (args.framesToSkip < 0) {
        std::cerr << "Frames must be a positive number." << std::endl;
        std::exit(EXIT_FAILURE);
    }

    if (args.secondsToSkip < 0) {
        std::cerr << "Seconds must be a positive number." << std::endl;
        std::exit(EXIT_FAILURE);
    }

    if (optind + 2 != argc) {
        std::cerr << "Error: Expected input and output file paths." << std::endl;
        printUsage(programName);
        std::exit(EXIT_FAILURE);
    } else {
        args.inputPath = argv[optind];
        args.outputPath = argv[optind + 1];
    }

    return args;
}


void createGammaLUT(unsigned char lut[256], float gamma_) {
    // Calculate every possible gamma value from 0 to 255 to speed up calculations later
    for (int i = 0; i < 256; ++i) {
        lut[i] = cv::saturate_cast<unsigned char>(cv::pow(i / 255.0, gamma_) * 255.0);
    }
}


void applyGammaCorrection(cv::Mat& image) {
    // Apply gamma correction using the lookup table
    // Each channel gets gamma correction applied
    cv::MatIterator_<cv::Vec3b> it, end;
    for (it = image.begin<cv::Vec3b>(), end = image.end<cv::Vec3b>(); it != end; ++it) {
        (*it)[0] = gammaLUT[(*it)[0]];
        (*it)[1] = gammaLUT[(*it)[1]];
        (*it)[2] = gammaLUT[(*it)[2]];
    }
}


void compareFrames(const cv::Mat& src1, const cv::Mat& src2, cv::Mat& dst) {
    dst = cv::Mat(src1.size(), src1.type());            // The output frame is the same size and type as the input frames

    cv::Mat inverted;
    cv::bitwise_not(src2, inverted);                    // Get a negative color image of the second frame using bitwise not

    cv::addWeighted(src1, 0.5, inverted, 0.5, 0, dst);  // Combine the first frame with the inverted frame
}


void extractMotion(cv::VideoCapture& inputVideo, cv::VideoWriter& outputVideo, unsigned long frameDelay, bool overlay) {
    std::queue<cv::Mat> frameQueue;     // Frame buffer to compare the current frame with old frames
    cv::Mat frame, firstFrame;

    // If frameDelay is 0, we compare all video frames with the very first frame and don't need to use the buffer
    if (frameDelay == 0) {
        inputVideo >> firstFrame;   // Save the first frame of the video
    }

    while (inputVideo.read(frame)) {
        // Fill the frame buffer with frameDelay number of frames before starting the comparisons
        if (frameDelay > 0) {
            frameQueue.push(frame.clone());
            if (frameQueue.size() < frameDelay + 1) continue;
        }

        cv::Mat outputFrame;
        // Again, if frameDelay is 0 we don't use the buffer
        if (frameDelay == 0) {
            compareFrames(frame, firstFrame, outputFrame);
        } else {
            compareFrames(frame, frameQueue.front(), outputFrame);
            frameQueue.pop();   // Remove the oldest frame from the buffer
        }

        // Overlay the motion frame over the original frame or apply some gamma correction to just the motion frame
        if (overlay) {
            // Convert the motion frame to grayscale
            cv::cvtColor(outputFrame, outputFrame, cv::COLOR_BGR2GRAY);
            // Set every pixel darker than (129, 129, 129) to black, and every pixel brighter to white
            cv::threshold(outputFrame, outputFrame, 129, 255, cv::THRESH_BINARY);
            // Apply some blur to make the overlay look a little nicer
            cv::blur(outputFrame, outputFrame, cv::Size(3,3));
            // Convert the grayscale image back to BGR (OpenCV default)
            cv::cvtColor(outputFrame, outputFrame, cv::COLOR_GRAY2BGR);
            // Using bitwise_or() will overlay the two frames
            cv::bitwise_or(frame, outputFrame, outputFrame);
        } else {
            applyGammaCorrection(outputFrame);
        }

        outputVideo.write(outputFrame);
    }
}
