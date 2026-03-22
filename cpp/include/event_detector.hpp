#pragma once

#include <memory>
#include <string>
#include <vector>

#include "roi_motion.hpp"
#include "types.hpp"

namespace fridge {

class IObjectClassifier {
public:
    virtual ~IObjectClassifier() = default;

    virtual std::vector<DetectedObject> classify(
        const GrayFrame& before_frame,
        const GrayFrame& after_frame,
        const std::vector<ChangeRegion>& regions
    ) const = 0;
};

class NullClassifier : public IObjectClassifier {
public:
    std::vector<DetectedObject> classify(
        const GrayFrame& before_frame,
        const GrayFrame& after_frame,
        const std::vector<ChangeRegion>& regions
    ) const override;
};

class EventDetector {
public:
    explicit EventDetector(
        DetectorConfig config = {},
        std::shared_ptr<IObjectClassifier> classifier = nullptr
    );

    EventResult detect(
        const SelectedFrames& selected_frames,
        const std::string& session_id,
        const std::string& before_frame_path,
        const std::string& after_frame_path
    ) const;

private:
    DetectorConfig config_;
    std::shared_ptr<IObjectClassifier> classifier_;
};

std::string event_result_to_json(const EventResult& result);

bool write_event_json(const EventResult& result, const std::string& output_path, std::string& error_message);

}  // namespace fridge
