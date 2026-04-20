#include "hand_skeleton_viewer.h"

#include <algorithm>
#include <cmath>

#include <opencv2/highgui.hpp>
#include <opencv2/imgproc.hpp>

namespace handdemo {

namespace {

constexpr int canvasWidth = 960;
constexpr int canvasHeight = 540;
constexpr int lineWidthValue = 2;
constexpr int pointRadiusValue = 5;
constexpr double textScaleValue = 0.5;
constexpr int textThicknessValue = 1;

// pseudoProjectXValue/pseudoProjectYValue:
// 这两个常量控制最终统一投影的方向和强度。
// 本版只追求轻微立体感，不做明显透视缩短。
constexpr double pseudoProjectXValue = -7.0;
constexpr double pseudoProjectYValue = -4.0;

// fixedBoneConnectionList: 固定 20 点骨架拓扑。
constexpr std::array<std::pair<int, int>, 20> fixedBoneConnectionList = {{
    {0, 1}, {1, 2}, {2, 3},
    {0, 4}, {4, 5}, {5, 6}, {6, 7},
    {4, 8}, {8, 9}, {9, 10}, {10, 11},
    {8, 12}, {12, 13}, {13, 14}, {14, 15},
    {12, 16}, {16, 17}, {17, 18}, {18, 19},
    {0, 16},
}};

// closedPosePointList: 20 点标准伸直模板
const std::array<cv::Point2f, SkeletonTemplate20::pointCount> closedPosePointList = {{
    cv::Point2f(475.0f, 418.0f),
    cv::Point2f(558.0f, 340.0f),
    cv::Point2f(589.0f, 298.0f),
    cv::Point2f(621.0f, 257.0f),
    cv::Point2f(516.0f, 267.0f),
    cv::Point2f(517.0f, 193.0f),
    cv::Point2f(517.0f, 139.0f),
    cv::Point2f(517.0f, 97.0f),
    cv::Point2f(476.0f, 265.0f),
    cv::Point2f(475.0f, 181.0f),
    cv::Point2f(474.0f, 123.0f),
    cv::Point2f(472.0f, 74.0f),
    cv::Point2f(435.0f, 274.0f),
    cv::Point2f(433.0f, 198.0f),
    cv::Point2f(432.0f, 147.0f),
    cv::Point2f(430.0f, 107.0f),
    cv::Point2f(399.0f, 289.0f),
    cv::Point2f(396.0f, 240.0f),
    cv::Point2f(395.0f, 196.0f),
    cv::Point2f(394.0f, 153.0f),
}};

// fistPosePointList: 握拳模板。
const std::array<cv::Point2f, SkeletonTemplate20::pointCount> fistPosePointList = {{
    cv::Point2f(462.0f, 455.0f),
    cv::Point2f(531.0f, 367.0f),
    cv::Point2f(540.0f, 316.0f),
    cv::Point2f(493.0f, 283.0f),
    cv::Point2f(519.0f, 268.0f),
    cv::Point2f(563.0f, 246.0f),
    cv::Point2f(557.0f, 285.0f),
    cv::Point2f(523.0f, 304.0f),
    cv::Point2f(476.0f, 266.0f),
    cv::Point2f(513.0f, 245.0f),
    cv::Point2f(517.0f, 287.0f),
    cv::Point2f(482.0f, 301.0f),
    cv::Point2f(436.0f, 274.0f),
    cv::Point2f(464.0f, 246.0f),
    cv::Point2f(470.0f, 290.0f),
    cv::Point2f(446.0f, 306.0f),
    cv::Point2f(399.0f, 290.0f),
    cv::Point2f(416.0f, 253.0f),
    cv::Point2f(431.0f, 295.0f),
    cv::Point2f(415.0f, 316.0f),
}};

// 颜色
const cv::Scalar backgroundColorValue(255, 255, 255);
const cv::Scalar boneColorValue(219, 12, 8);
const cv::Scalar pointColorValue(79, 19, 242);
const cv::Scalar textColorValue(22, 115, 249);

double clampValue(double rawValue, double minValue, double maxValue) {
    if (rawValue < minValue) {
        return minValue;
    }
    if (rawValue > maxValue) {
        return maxValue;
    }
    return rawValue;
}

cv::Point2f subtractPoint(const cv::Point2f& endPointValue, const cv::Point2f& startPointValue) {
    return cv::Point2f(
        endPointValue.x - startPointValue.x,
        endPointValue.y - startPointValue.y);
}

cv::Point2f addPoint(const cv::Point2f& basePointValue, const cv::Point2f& offsetPointValue) {
    return cv::Point2f(
        basePointValue.x + offsetPointValue.x,
        basePointValue.y + offsetPointValue.y);
}

cv::Point toCanvasPoint(const cv::Point2f& pointValue) {
    return cv::Point(
        static_cast<int>(std::lround(pointValue.x)),
        static_cast<int>(std::lround(pointValue.y)));
}

double getVectorAngleDegree(const cv::Point2f& vectorValue) {
    return std::atan2(
               static_cast<double>(vectorValue.y),
               static_cast<double>(vectorValue.x)) *
        180.0 / 3.14159265358979323846;
}

double normalizeAngleDelta(double angleDegreeValue) {
    double normalizedValue = angleDegreeValue;
    while (normalizedValue > 180.0) {
        normalizedValue -= 360.0;
    }
    while (normalizedValue < -180.0) {
        normalizedValue += 360.0;
    }
    return normalizedValue;
}

double getSignedAngleDegree(const cv::Point2f& fromVector, const cv::Point2f& toVector) {
    return normalizeAngleDelta(getVectorAngleDegree(toVector) - getVectorAngleDegree(fromVector));
}

double getSegmentLength(const cv::Point2f& startPointValue, const cv::Point2f& endPointValue) {
    const double deltaX = static_cast<double>(endPointValue.x - startPointValue.x);
    const double deltaY = static_cast<double>(endPointValue.y - startPointValue.y);
    return std::sqrt(deltaX * deltaX + deltaY * deltaY);
}

cv::Point2f buildPointByAngle(const cv::Point2f& startPointValue, double angleDegreeValue, double segmentLengthValue) {
    const double angleRadianValue = angleDegreeValue * 3.14159265358979323846 / 180.0;
    return cv::Point2f(
        static_cast<float>(startPointValue.x + std::cos(angleRadianValue) * segmentLengthValue),
        static_cast<float>(startPointValue.y + std::sin(angleRadianValue) * segmentLengthValue));
}

double interpolateAngleByRatio(double startAngleValue, double endAngleValue, double ratioValue) {
    return startAngleValue + ratioValue * normalizeAngleDelta(endAngleValue - startAngleValue);
}

double getAngleRatio(double currentAngleValue, double maxAngleValue) {
    if (maxAngleValue <= 0.0001) {
        return 0.0;
    }
    return clampValue(std::abs(currentAngleValue) / maxAngleValue, 0.0, 1.0);
}

double getFingerDisplayFlexRatio(const float angleValueList[3], const FingerFlexAngleModel& flexAngleModel) {
    return std::max({
        getAngleRatio(static_cast<double>(angleValueList[0]), flexAngleModel.rootHoldDeltaAngle),
        getAngleRatio(static_cast<double>(angleValueList[1]), flexAngleModel.jointHoldDeltaAngle1),
        getAngleRatio(static_cast<double>(angleValueList[2]), flexAngleModel.jointHoldDeltaAngle2),
    });
}

double getThumbDisplayFlexRatio(const HandAngleOutput& outputValue) {
    return std::max(
        getAngleRatio(static_cast<double>(outputValue.thumb[0]), kThumbFlexAngleModel.mcpHoldDeltaAngle),
        getAngleRatio(static_cast<double>(outputValue.thumb[1]), kThumbFlexAngleModel.ipHoldDeltaAngle));
}

}  // namespace

HandSkeletonViewer::HandSkeletonViewer()
    : windowNameText_("20 Point Hand Skeleton") {
    skeletonTemplate_.imageWidth = canvasWidth;
    skeletonTemplate_.imageHeight = canvasHeight;
    skeletonTemplate_.pointList = closedPosePointList;

    fistPoseTemplate_.imageWidth = canvasWidth;
    fistPoseTemplate_.imageHeight = canvasHeight;
    fistPoseTemplate_.pointList = fistPosePointList;

    // fingerFlexSegmentMap_: 这里把五指真正参与弯折的关节段固定下来，后续联调都按这套关系走。
    fingerFlexSegmentMap_.thumbSegmentList = {{{1, 2}, {2, 3}}};
    fingerFlexSegmentMap_.indexSegmentList = {{{4, 5}, {5, 6}, {6, 7}}};
    fingerFlexSegmentMap_.middleSegmentList = {{{8, 9}, {9, 10}, {10, 11}}};
    fingerFlexSegmentMap_.ringSegmentList = {{{12, 13}, {13, 14}, {14, 15}}};
    fingerFlexSegmentMap_.littleSegmentList = {{{16, 17}, {17, 18}, {18, 19}}};

    // buildKinematicModel: 启动时先把闭合/握拳模板转成局部角模型，运行时只做插值和重建。
    buildKinematicModel();
    currentSkeleton_ = skeletonTemplate_;
}

void HandSkeletonViewer::buildKinematicModel() {
    const auto buildFingerModel = [this](
                                      int parentStartId,
                                      int parentEndId,
                                      int pointId1,
                                      int pointId2,
                                      int pointId3) {
        FingerKinematicModel fingerKinematicModel;
        const auto& closePointList = skeletonTemplate_.pointList;
        const auto& fistPointList = fistPoseTemplate_.pointList;

        // segmentLengthList: 显示层骨长固定使用闭合模板，避免运行时出现异常缩短。
        fingerKinematicModel.segmentLengthList[0] = getSegmentLength(
            closePointList[static_cast<std::size_t>(parentEndId)],
            closePointList[static_cast<std::size_t>(pointId1)]);
        fingerKinematicModel.segmentLengthList[1] = getSegmentLength(
            closePointList[static_cast<std::size_t>(pointId1)],
            closePointList[static_cast<std::size_t>(pointId2)]);
        fingerKinematicModel.segmentLengthList[2] = getSegmentLength(
            closePointList[static_cast<std::size_t>(pointId2)],
            closePointList[static_cast<std::size_t>(pointId3)]);

        fingerKinematicModel.closeLocalAngleList[0] = getSignedAngleDegree(
            subtractPoint(closePointList[static_cast<std::size_t>(parentEndId)], closePointList[static_cast<std::size_t>(parentStartId)]),
            subtractPoint(closePointList[static_cast<std::size_t>(pointId1)], closePointList[static_cast<std::size_t>(parentEndId)]));
        fingerKinematicModel.closeLocalAngleList[1] = getSignedAngleDegree(
            subtractPoint(closePointList[static_cast<std::size_t>(pointId1)], closePointList[static_cast<std::size_t>(parentEndId)]),
            subtractPoint(closePointList[static_cast<std::size_t>(pointId2)], closePointList[static_cast<std::size_t>(pointId1)]));
        fingerKinematicModel.closeLocalAngleList[2] = getSignedAngleDegree(
            subtractPoint(closePointList[static_cast<std::size_t>(pointId2)], closePointList[static_cast<std::size_t>(pointId1)]),
            subtractPoint(closePointList[static_cast<std::size_t>(pointId3)], closePointList[static_cast<std::size_t>(pointId2)]));

        fingerKinematicModel.fistLocalAngleList[0] = getSignedAngleDegree(
            subtractPoint(fistPointList[static_cast<std::size_t>(parentEndId)], fistPointList[static_cast<std::size_t>(parentStartId)]),
            subtractPoint(fistPointList[static_cast<std::size_t>(pointId1)], fistPointList[static_cast<std::size_t>(parentEndId)]));
        fingerKinematicModel.fistLocalAngleList[1] = getSignedAngleDegree(
            subtractPoint(fistPointList[static_cast<std::size_t>(pointId1)], fistPointList[static_cast<std::size_t>(parentEndId)]),
            subtractPoint(fistPointList[static_cast<std::size_t>(pointId2)], fistPointList[static_cast<std::size_t>(pointId1)]));
        fingerKinematicModel.fistLocalAngleList[2] = getSignedAngleDegree(
            subtractPoint(fistPointList[static_cast<std::size_t>(pointId2)], fistPointList[static_cast<std::size_t>(pointId1)]),
            subtractPoint(fistPointList[static_cast<std::size_t>(pointId3)], fistPointList[static_cast<std::size_t>(pointId2)]));
        return fingerKinematicModel;
    };

    fingerKinematicModelByIndex_[static_cast<std::size_t>(FourFingerIndex::Index)] =
        buildFingerModel(0, 4, 5, 6, 7);
    fingerKinematicModelByIndex_[static_cast<std::size_t>(FourFingerIndex::Middle)] =
        buildFingerModel(4, 8, 9, 10, 11);
    fingerKinematicModelByIndex_[static_cast<std::size_t>(FourFingerIndex::Ring)] =
        buildFingerModel(8, 12, 13, 14, 15);
    fingerKinematicModelByIndex_[static_cast<std::size_t>(FourFingerIndex::Little)] =
        buildFingerModel(12, 16, 17, 18, 19);

    const auto& closePointList = skeletonTemplate_.pointList;
    const auto& fistPointList = fistPoseTemplate_.pointList;
    thumbKinematicModel_.segmentLengthList[0] = getSegmentLength(closePointList[1], closePointList[2]);
    thumbKinematicModel_.segmentLengthList[1] = getSegmentLength(closePointList[2], closePointList[3]);
    thumbKinematicModel_.closeLocalAngleList[0] = getSignedAngleDegree(
        subtractPoint(closePointList[1], closePointList[0]),
        subtractPoint(closePointList[2], closePointList[1]));
    thumbKinematicModel_.closeLocalAngleList[1] = getSignedAngleDegree(
        subtractPoint(closePointList[2], closePointList[1]),
        subtractPoint(closePointList[3], closePointList[2]));
    thumbKinematicModel_.fistLocalAngleList[0] = getSignedAngleDegree(
        subtractPoint(fistPointList[1], fistPointList[0]),
        subtractPoint(fistPointList[2], fistPointList[1]));
    thumbKinematicModel_.fistLocalAngleList[1] = getSignedAngleDegree(
        subtractPoint(fistPointList[2], fistPointList[1]),
        subtractPoint(fistPointList[3], fistPointList[2]));
}

void HandSkeletonViewer::resetPose() {
    currentSkeleton_ = skeletonTemplate_;
}

void HandSkeletonViewer::updateFromAngles(const HandAngleOutput& outputValue) {
    // resetPose: 每帧都从闭合模板开始重建，避免累计误差把骨架越带越歪。
    resetPose();

    // pointDepthValueList:
    // 这里记录每个关节点的“虚拟深度值”。
    // 先按二维前向运动学得到标准点位，最后再统一做一次轻量投影变换。
    std::array<double, SkeletonTemplate20::pointCount> pointDepthValueList{};

    rebuildThumbPointList(outputValue, pointDepthValueList);
    rebuildFingerPointList(
        currentSkeleton_.pointList[0],
        currentSkeleton_.pointList[4],
        outputValue.index_finger,
        fingerKinematicModelByIndex_[static_cast<std::size_t>(FourFingerIndex::Index)],
        kFingerFlexAngleModelByIndex[static_cast<std::size_t>(FourFingerIndex::Index)],
        5,
        6,
        7,
        pointDepthValueList);
    rebuildFingerPointList(
        currentSkeleton_.pointList[4],
        currentSkeleton_.pointList[8],
        outputValue.middle_finger,
        fingerKinematicModelByIndex_[static_cast<std::size_t>(FourFingerIndex::Middle)],
        kFingerFlexAngleModelByIndex[static_cast<std::size_t>(FourFingerIndex::Middle)],
        9,
        10,
        11,
        pointDepthValueList);
    rebuildFingerPointList(
        currentSkeleton_.pointList[8],
        currentSkeleton_.pointList[12],
        outputValue.ring_finger,
        fingerKinematicModelByIndex_[static_cast<std::size_t>(FourFingerIndex::Ring)],
        kFingerFlexAngleModelByIndex[static_cast<std::size_t>(FourFingerIndex::Ring)],
        13,
        14,
        15,
        pointDepthValueList);
    rebuildFingerPointList(
        currentSkeleton_.pointList[12],
        currentSkeleton_.pointList[16],
        outputValue.little_finger,
        fingerKinematicModelByIndex_[static_cast<std::size_t>(FourFingerIndex::Little)],
        kFingerFlexAngleModelByIndex[static_cast<std::size_t>(FourFingerIndex::Little)],
        17,
        18,
        19,
        pointDepthValueList);

    // applyPseudoDepthProjection: 所有点都重建完成后，再统一做一次轻量投影。
    // 这样立体感来自“最终显示变换”，而不是把每段骨长直接缩短。
    applyPseudoDepthProjection(pointDepthValueList);
}

void HandSkeletonViewer::rebuildThumbPointList(
    const HandAngleOutput& outputValue,
    std::array<double, SkeletonTemplate20::pointCount>& pointDepthValueList) {
    const double segment12Ratio = getAngleRatio(
        static_cast<double>(outputValue.thumb[0]),
        kThumbFlexAngleModel.mcpHoldDeltaAngle);
    const double segment23Ratio = getAngleRatio(
        static_cast<double>(outputValue.thumb[1]),
        kThumbFlexAngleModel.ipHoldDeltaAngle);
    const double displayFlexRatio = std::max(segment12Ratio, segment23Ratio);

    // baseAngleValue: 拇指 0->1 根骨保持闭合模板方向，只让后两段按模板局部角卷曲。
    const double baseAngleValue = getVectorAngleDegree(
        subtractPoint(currentSkeleton_.pointList[1], currentSkeleton_.pointList[0]));
    const double segment12LocalAngleValue = interpolateAngleByRatio(
        thumbKinematicModel_.closeLocalAngleList[0],
        thumbKinematicModel_.fistLocalAngleList[0],
        segment12Ratio);
    const double segment12AngleValue = baseAngleValue + segment12LocalAngleValue;
    const cv::Point2f point2 = buildPointByAngle(
        currentSkeleton_.pointList[1],
        segment12AngleValue,
        thumbKinematicModel_.segmentLengthList[0]);

    const double segment23LocalAngleValue = interpolateAngleByRatio(
        thumbKinematicModel_.closeLocalAngleList[1],
        thumbKinematicModel_.fistLocalAngleList[1],
        segment23Ratio);
    const double segment23AngleValue = segment12AngleValue + segment23LocalAngleValue;
    const cv::Point2f point3 = buildPointByAngle(
        point2,
        segment23AngleValue,
        thumbKinematicModel_.segmentLengthList[1]);

    currentSkeleton_.pointList[2] = point2;
    currentSkeleton_.pointList[3] = point3;

    // 虚拟深度只用于最终轻量投影，越远端权重越高。
    pointDepthValueList[2] = std::max(pointDepthValueList[2], displayFlexRatio * 0.95);
    pointDepthValueList[3] = std::max(pointDepthValueList[3], displayFlexRatio * 1.35);
}

void HandSkeletonViewer::rebuildFingerPointList(
    const cv::Point2f& parentStartPoint,
    const cv::Point2f& rootPoint,
    const float angleValueList[3],
    const FingerKinematicModel& fingerKinematicModel,
    const FingerFlexAngleModel& flexAngleModel,
    int pointId1,
    int pointId2,
    int pointId3,
    std::array<double, SkeletonTemplate20::pointCount>& pointDepthValueList) {
    // 先把输出角换成 0~1 的比例，再用闭合/握拳模板插值得到当前局部角。
    const double rootRatio = getAngleRatio(
        static_cast<double>(angleValueList[0]),
        flexAngleModel.rootHoldDeltaAngle);
    const double jointRatio1 = getAngleRatio(
        static_cast<double>(angleValueList[1]),
        flexAngleModel.jointHoldDeltaAngle1);
    const double jointRatio2 = getAngleRatio(
        static_cast<double>(angleValueList[2]),
        flexAngleModel.jointHoldDeltaAngle2);
    const double displayFlexRatio = std::max({rootRatio, jointRatio1, jointRatio2});

    const double parentAngleValue = getVectorAngleDegree(subtractPoint(rootPoint, parentStartPoint));
    const double segment1LocalAngleValue = interpolateAngleByRatio(
        fingerKinematicModel.closeLocalAngleList[0],
        fingerKinematicModel.fistLocalAngleList[0],
        rootRatio);
    const double segment1AngleValue = parentAngleValue + segment1LocalAngleValue;
    const cv::Point2f point1 = buildPointByAngle(
        rootPoint,
        segment1AngleValue,
        fingerKinematicModel.segmentLengthList[0]);

    const double segment2LocalAngleValue = interpolateAngleByRatio(
        fingerKinematicModel.closeLocalAngleList[1],
        fingerKinematicModel.fistLocalAngleList[1],
        jointRatio1);
    const double segment2AngleValue = segment1AngleValue + segment2LocalAngleValue;
    const cv::Point2f point2 = buildPointByAngle(
        point1,
        segment2AngleValue,
        fingerKinematicModel.segmentLengthList[1]);

    const double segment3LocalAngleValue = interpolateAngleByRatio(
        fingerKinematicModel.closeLocalAngleList[2],
        fingerKinematicModel.fistLocalAngleList[2],
        jointRatio2);
    const double segment3AngleValue = segment2AngleValue + segment3LocalAngleValue;
    const cv::Point2f point3 = buildPointByAngle(
        point2,
        segment3AngleValue,
        fingerKinematicModel.segmentLengthList[2]);

    currentSkeleton_.pointList[static_cast<std::size_t>(pointId1)] = point1;
    currentSkeleton_.pointList[static_cast<std::size_t>(pointId2)] = point2;
    currentSkeleton_.pointList[static_cast<std::size_t>(pointId3)] = point3;

    pointDepthValueList[static_cast<std::size_t>(pointId1)] = std::max(
        pointDepthValueList[static_cast<std::size_t>(pointId1)],
        displayFlexRatio * 0.55);
    pointDepthValueList[static_cast<std::size_t>(pointId2)] = std::max(
        pointDepthValueList[static_cast<std::size_t>(pointId2)],
        displayFlexRatio * 0.95);
    pointDepthValueList[static_cast<std::size_t>(pointId3)] = std::max(
        pointDepthValueList[static_cast<std::size_t>(pointId3)],
        displayFlexRatio * 1.35);
}

void HandSkeletonViewer::applyPseudoDepthProjection(
    const std::array<double, SkeletonTemplate20::pointCount>& pointDepthValueList) {
    for (std::size_t pointIndex = 0; pointIndex < SkeletonTemplate20::pointCount; ++pointIndex) {
        const double depthValue = pointDepthValueList[pointIndex];
        if (depthValue <= 0.0001) {
            continue;
        }

        // 这里的 depthValue 不是物理 Z 坐标，只是显示层的投影权重。
        currentSkeleton_.pointList[pointIndex] = addPoint(
            currentSkeleton_.pointList[pointIndex],
            cv::Point2f(
                static_cast<float>(depthValue * pseudoProjectXValue),
                static_cast<float>(depthValue * pseudoProjectYValue)));
    }
}

int HandSkeletonViewer::showWindowFrame() {
    if (!hasCreatedWindow_) {
        cv::namedWindow(windowNameText_, cv::WINDOW_AUTOSIZE);
        hasCreatedWindow_ = true;
    }

    if (cv::getWindowProperty(windowNameText_, cv::WND_PROP_VISIBLE) < 1.0) {
        return 27;
    }

    cv::imshow(windowNameText_, buildCanvasImage());
    return cv::waitKey(1);
}

bool HandSkeletonViewer::shouldQuitFromKey(int keyValue) const {
    return keyValue == 27 || keyValue == 'q' || keyValue == 'Q';
}

void HandSkeletonViewer::closeWindow() {
    if (hasCreatedWindow_) {
        cv::destroyWindow(windowNameText_);
        hasCreatedWindow_ = false;
    }
}

const SkeletonTemplate20& HandSkeletonViewer::getSkeletonTemplate() const {
    return skeletonTemplate_;
}

const FingerFlexSegmentMap& HandSkeletonViewer::getFingerFlexSegmentMap() const {
    return fingerFlexSegmentMap_;
}

cv::Mat HandSkeletonViewer::buildCanvasImage() const {
    cv::Mat canvasImage(canvasHeight, canvasWidth, CV_8UC3, backgroundColorValue);

    for (const auto& connectionItem : fixedBoneConnectionList) {
        const cv::Point startPoint = toCanvasPoint(currentSkeleton_.pointList[static_cast<std::size_t>(connectionItem.first)]);
        const cv::Point endPoint = toCanvasPoint(currentSkeleton_.pointList[static_cast<std::size_t>(connectionItem.second)]); 
        cv::line(canvasImage, startPoint, endPoint, boneColorValue, lineWidthValue, cv::LINE_AA);
    }

    for (std::size_t pointIndex = 0; pointIndex < SkeletonTemplate20::pointCount; ++pointIndex) {
        const cv::Point pointValue = toCanvasPoint(currentSkeleton_.pointList[pointIndex]);
        cv::circle(canvasImage, pointValue, pointRadiusValue, pointColorValue, cv::FILLED, cv::LINE_AA);
        cv::putText(
            canvasImage,
            std::to_string(pointIndex),
            cv::Point(pointValue.x + 6, pointValue.y - 6),
            cv::FONT_HERSHEY_SIMPLEX,
            textScaleValue,
            textColorValue,
            textThicknessValue,
            cv::LINE_AA);
    }

    return canvasImage;
}

}  // namespace handdemo
