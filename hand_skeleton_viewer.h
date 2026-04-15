#pragma once

#include <array>
#include <string>
#include <utility>

#include <opencv2/core.hpp>

#include "hand_algorithm.h"

namespace handdemo {

struct SkeletonTemplate20 {
    static constexpr std::size_t pointCount = 20;

    int imageWidth = 960;
    int imageHeight = 540;
    std::array<cv::Point2f, pointCount> pointList{};
};

struct FingerFlexSegmentMap {
    std::array<std::pair<int, int>, 2> thumbSegmentList{};
    std::array<std::pair<int, int>, 3> indexSegmentList{};
    std::array<std::pair<int, int>, 3> middleSegmentList{};
    std::array<std::pair<int, int>, 3> ringSegmentList{};
    std::array<std::pair<int, int>, 3> littleSegmentList{};
};

class HandSkeletonViewer {
public:
    HandSkeletonViewer();

    // resetPose: 把当前显示骨架恢复为闭合模板。
    void resetPose();
    // updateFromAngles: 根据算法输出角度重建 20 点骨架，并叠加轻量 2.5D 投影效果。
    void updateFromAngles(const HandAngleOutput& outputValue);
    // showWindowFrame: 刷新 OpenCV 窗口并返回当前按键值。
    int showWindowFrame();
    // shouldQuitFromKey: 判断当前按键是否为退出键。
    bool shouldQuitFromKey(int keyValue) const;
    // closeWindow: 主动关闭 OpenCV 窗口。
    void closeWindow();

    // getSkeletonTemplate: 返回闭合模板骨架，便于外部调试对照点位。
    const SkeletonTemplate20& getSkeletonTemplate() const;
    // getFingerFlexSegmentMap: 返回五指可弯折段映射关系。
    const FingerFlexSegmentMap& getFingerFlexSegmentMap() const;

private:
    struct FingerKinematicModel {
        // segmentLengthList: 三段骨长，始终以闭合模板为准。
        std::array<double, 3> segmentLengthList{};

        // closeLocalAngleList/fistLocalAngleList:
        // 分别保存闭合模板和握拳模板下，每一段相对父段的局部角。
        std::array<double, 3> closeLocalAngleList{};
        std::array<double, 3> fistLocalAngleList{};
    };

    struct ThumbKinematicModel {
        // segmentLengthList: 拇指两段骨长，始终以闭合模板为准。
        std::array<double, 2> segmentLengthList{};

        // closeLocalAngleList/fistLocalAngleList:
        // 保存 1->2、2->3 两段在闭合/握拳模板下的局部角。
        std::array<double, 2> closeLocalAngleList{};
        std::array<double, 2> fistLocalAngleList{};
    };

    void buildKinematicModel();
    void rebuildThumbPointList(const HandAngleOutput& outputValue, std::array<double, SkeletonTemplate20::pointCount>& pointDepthValueList);
    void rebuildFingerPointList(
        const cv::Point2f& parentStartPoint,
        const cv::Point2f& rootPoint,
        const float angleValueList[3],
        const FingerKinematicModel& fingerKinematicModel,
        const FingerFlexAngleModel& flexAngleModel,
        int pointId1,
        int pointId2,
        int pointId3,
        std::array<double, SkeletonTemplate20::pointCount>& pointDepthValueList);
    void applyPseudoDepthProjection(const std::array<double, SkeletonTemplate20::pointCount>& pointDepthValueList);
    cv::Mat buildCanvasImage() const;

    SkeletonTemplate20 skeletonTemplate_{};
    SkeletonTemplate20 fistPoseTemplate_{};
    SkeletonTemplate20 currentSkeleton_{};
    FingerFlexSegmentMap fingerFlexSegmentMap_{};
    std::array<FingerKinematicModel, 4> fingerKinematicModelByIndex_{};
    ThumbKinematicModel thumbKinematicModel_{};
    std::string windowNameText_;
    bool hasCreatedWindow_ = false;
};

}  // namespace handdemo
