/*
 * Copyright 2010 SRI International
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <sstream>
#include <fstream>
#include <stdexcept>
#include <queue>
#include <set>
#include <list>
#include <iterator>

#include <math.h>
#include <assert.h>

#include "open_karto/Mapper.h"

namespace karto
{

  // enable this for verbose debug information
  // #define KARTO_DEBUG

  #define MAX_VARIANCE            500.0
  #define DISTANCE_PENALTY_GAIN   0.2
  #define ANGLE_PENALTY_GAIN      0.2

  ////////////////////////////////////////////////////////////////////////////////////////
  ////////////////////////////////////////////////////////////////////////////////////////
  ////////////////////////////////////////////////////////////////////////////////////////
  //从成员变量来看，这个是存储了 scan和 runningscan的类，非常重要，同时这个又会被另一个类使用，形式是map<name, ScanManager>
  /**
   * Manages the scan data for a device
   */
  class ScanManager
  {
  public:
    /**
     * Default constructor
     */
    ScanManager(kt_int32u runningBufferMaximumSize, kt_double runningBufferMaximumDistance)
      : m_pLastScan(NULL)
      , m_RunningBufferMaximumSize(runningBufferMaximumSize)
      , m_RunningBufferMaximumDistance(runningBufferMaximumDistance)
    {
    }

    /**
     * Destructor
     */
    virtual ~ScanManager()
    {
      Clear();
    }

  public:
    /**
     * Adds scan to vector of processed scans tagging scan with given unique id
     * @param pScan
     */
    inline void AddScan(LocalizedRangeScan* pScan, kt_int32s uniqueId)
    {
      // assign state id to scan
      pScan->SetStateId(static_cast<kt_int32u>(m_Scans.size()));

      // assign unique id to scan
      pScan->SetUniqueId(uniqueId);

      // add it to scan buffer
      m_Scans.push_back(pScan);
    }

    /**
     * Gets last scan
     * @param deviceId
     * @return last localized range scan
     */
    inline LocalizedRangeScan* GetLastScan()
    {
      return m_pLastScan;
    }

    /**
     * Sets the last scan
     * @param pScan
     */
    inline void SetLastScan(LocalizedRangeScan* pScan)
    {
      m_pLastScan = pScan;
    }

    /**
     * Gets scans
     * @return scans
     */
    inline LocalizedRangeScanVector& GetScans()
    {
      return m_Scans;
    }

    /**
     * Gets running scans
     * @return running scans
     */
    inline LocalizedRangeScanVector& GetRunningScans()
    {
      return m_RunningScans;
    }

    /**
     * Adds scan to vector of running scans
     * @param pScan
     */
    void AddRunningScan(LocalizedRangeScan* pScan)
    {
      m_RunningScans.push_back(pScan);

      // vector has at least one element (first line of this function), so this is valid
      Pose2 frontScanPose = m_RunningScans.front()->GetSensorPose();
      Pose2 backScanPose = m_RunningScans.back()->GetSensorPose();

      // cap vector size and remove all scans from front of vector that are too far from end of vector
      //进行 runningScan的维护，数量上的维护以及第一个与最后一个距离之间的维护
      kt_double squaredDistance = frontScanPose.GetPosition().SquaredDistance(backScanPose.GetPosition());
      while (m_RunningScans.size() > m_RunningBufferMaximumSize ||
             squaredDistance > math::Square(m_RunningBufferMaximumDistance) - KT_TOLERANCE)
      {
        // remove front of running scans
        m_RunningScans.erase(m_RunningScans.begin());

        // recompute stats of running scans
        frontScanPose = m_RunningScans.front()->GetSensorPose();
        backScanPose = m_RunningScans.back()->GetSensorPose();
        squaredDistance = frontScanPose.GetPosition().SquaredDistance(backScanPose.GetPosition());
      }
    }

    /**
     * Deletes data of this buffered device
     */
    void Clear()
    {
      m_Scans.clear();
      m_RunningScans.clear();
    }

  private:
    LocalizedRangeScanVector m_Scans;
    LocalizedRangeScanVector m_RunningScans;
    LocalizedRangeScan* m_pLastScan;

    kt_int32u m_RunningBufferMaximumSize;
    kt_double m_RunningBufferMaximumDistance;
  };  // ScanManager

  ////////////////////////////////////////////////////////////////////////////////////////
  ////////////////////////////////////////////////////////////////////////////////////////
  ////////////////////////////////////////////////////////////////////////////////////////

  void MapperSensorManager::RegisterSensor(const Name& rSensorName)
  {
    if (GetScanManager(rSensorName) == NULL)
    {
      m_ScanManagers[rSensorName] = new ScanManager(m_RunningBufferMaximumSize, m_RunningBufferMaximumDistance);
    }
  }


  /**
   * Gets scan from given device with given ID
   * @param rSensorName
   * @param scanNum
   * @return localized range scan
   */
  LocalizedRangeScan* MapperSensorManager::GetScan(const Name& rSensorName, kt_int32s scanIndex)
  {
    ScanManager* pScanManager = GetScanManager(rSensorName);
    if (pScanManager != NULL)
    {
      return pScanManager->GetScans().at(scanIndex);
    }

    assert(false);
    return NULL;
  }

  /**
   * Gets last scan of given device
   * @param pLaserRangeFinder
   * @return last localized range scan of device
   */
  inline LocalizedRangeScan* MapperSensorManager::GetLastScan(const Name& rSensorName)
  {
    RegisterSensor(rSensorName);

    return GetScanManager(rSensorName)->GetLastScan();
  }

  /**
   * Sets the last scan of device of given scan
   * @param pScan
   */
  inline void MapperSensorManager::SetLastScan(LocalizedRangeScan* pScan)
  {
    GetScanManager(pScan)->SetLastScan(pScan);
  }

  /**
   * Adds scan to scan vector of device that recorded scan
   * @param pScan
   */
  void MapperSensorManager::AddScan(LocalizedRangeScan* pScan)
  {
    GetScanManager(pScan)->AddScan(pScan, m_NextScanId);   //这个是在ScanManager 类中加入了 pScan
    m_Scans.push_back(pScan);                              //这个是在当前的MapperSensorManager 中加入了 pScan，两者有关系
    m_NextScanId++;                                        //MapperSensorManager has a "typedef std::map<Name, ScanManager*> ScanManagerMap"
  }

  /**
   * Adds scan to running scans of device that recorded scan
   * @param pScan
   */
  inline void MapperSensorManager::AddRunningScan(LocalizedRangeScan* pScan)
  {
    GetScanManager(pScan)->AddRunningScan(pScan);
  }

  /**
   * Gets scans of device
   * @param rSensorName
   * @return scans of device
   */
  inline LocalizedRangeScanVector& MapperSensorManager::GetScans(const Name& rSensorName)
  {
    return GetScanManager(rSensorName)->GetScans();
  }

  /**
   * Gets running scans of device
   * @param rSensorName
   * @return running scans of device
   */
  inline LocalizedRangeScanVector& MapperSensorManager::GetRunningScans(const Name& rSensorName)
  {
    return GetScanManager(rSensorName)->GetRunningScans();
  }

  /**
   * Gets all scans of all devices
   * @return all scans of all devices
   */
  LocalizedRangeScanVector MapperSensorManager::GetAllScans()
  {
    LocalizedRangeScanVector scans;

    forEach(ScanManagerMap, &m_ScanManagers)
    {
      LocalizedRangeScanVector& rScans = iter->second->GetScans();

      scans.insert(scans.end(), rScans.begin(), rScans.end());
    }

    return scans;
  }

  /**
   * Deletes all scan managers of all devices
   */
  void MapperSensorManager::Clear()
  {
//    SensorManager::Clear();

    forEach(ScanManagerMap, &m_ScanManagers)
    {
      delete iter->second;
    }

    m_ScanManagers.clear();
  }

  ////////////////////////////////////////////////////////////////////////////////////////
  ////////////////////////////////////////////////////////////////////////////////////////
  ////////////////////////////////////////////////////////////////////////////////////////

  ScanMatcher::~ScanMatcher()
  {
    delete m_pCorrelationGrid;
    delete m_pSearchSpaceProbs;
    delete m_pGridLookup;
  }

  ScanMatcher* ScanMatcher::Create(Mapper* pMapper, kt_double searchSize, kt_double resolution,
                                   kt_double smearDeviation, kt_double rangeThreshold)
  {
    // invalid parameters
    if (resolution <= 0)
    {
      return NULL;
    }
    if (searchSize <= 0)
    {
      return NULL;
    }
    if (smearDeviation < 0)
    {
      return NULL;
    }
    if (rangeThreshold <= 0)
    {
      return NULL;
    }

    assert(math::DoubleEqual(math::Round(searchSize / resolution), (searchSize / resolution)));

    // calculate search space in grid coordinates   //searchSize大小是0.3，既然和rangeThreshold有相同单位，就是说匹配范围扩大0.3m，类似于卷积核大小是0.3m
    kt_int32u searchSpaceSideSize = static_cast<kt_int32u>(math::Round(searchSize / resolution) + 1);

    // compute requisite size of correlation grid (pad grid so that scan points can't fall off the grid
    // if a scan is on the border of the search space)
    kt_int32u pointReadingMargin = static_cast<kt_int32u>(ceil(rangeThreshold / resolution));
    //这里写出了应该搜索的范围的grid
    kt_int32s gridSize = searchSpaceSideSize + 2 * pointReadingMargin;

    // create correlation grid
    assert(gridSize % 2 == 1);
    //CorrelationGrid 和 Grid<kt_double> 都是 Grid<T>的形式，但是这里面有borderSize
    //作为x,y的坐标，实在是不能理解
    CorrelationGrid* pCorrelationGrid = CorrelationGrid::CreateGrid(gridSize, gridSize, resolution, smearDeviation);

    // create search space probabilities
    Grid<kt_double>* pSearchSpaceProbs = Grid<kt_double>::CreateGrid(searchSpaceSideSize,
                                                                     searchSpaceSideSize, resolution);

    ScanMatcher* pScanMatcher = new ScanMatcher(pMapper);
    pScanMatcher->m_pCorrelationGrid = pCorrelationGrid;
    pScanMatcher->m_pSearchSpaceProbs = pSearchSpaceProbs;
    pScanMatcher->m_pGridLookup = new GridIndexLookup<kt_int8u>(pCorrelationGrid);

    return pScanMatcher;
  }

  /**
   * Match given scan against set of scans
   * @param pScan scan being scan-matched
   * @param rBaseScans set of scans whose points will mark cells in grid as being occupied
   * @param rMean output parameter of mean (best pose) of match
   * @param rCovariance output parameter of covariance of match
   * @param doPenalize whether to penalize matches further from the search center
   * @param doRefineMatch whether to do finer-grained matching if coarse match is good (default is true)
   * @return strength of response
   */
  kt_double ScanMatcher::MatchScan(LocalizedRangeScan* pScan, const LocalizedRangeScanVector& rBaseScans, Pose2& rMean,
                                   Matrix3& rCovariance, kt_bool doPenalize, kt_bool doRefineMatch)
  {
    ///////////////////////////////////////
    // set scan pose to be center of grid
    //pScan还没有更新父类LaserRangeScan中原始数据，扫描深度数据都在父类中，通过 pScan->GetRangeReadings()[iterNum]这种形式来访问。
    // 1. get scan position
    Pose2 scanPose = pScan->GetSensorPose();   // 第二个数据进来时，scanPose的位置是(1,1) angle是 90度

    // scan has no readings; cannot do scan matching
    // best guess of pose is based off of adjusted odometer reading
    if (pScan->GetNumberOfRangeReadings() == 0)
    {
      rMean = scanPose;

      // maximum covariance
      rCovariance(0, 0) = MAX_VARIANCE;  // XX
      rCovariance(1, 1) = MAX_VARIANCE;  // YY
      rCovariance(2, 2) = 4 * math::Square(m_pMapper->m_pCoarseAngleResolution->GetValue());  // TH*TH

      return 0.0;
    }

    // 2. get size of grid
    Rectangle2<kt_int32s> roi = m_pCorrelationGrid->GetROI();   // 这个是栅格地图，12（雷达范围,之前设置过的 SetRangeThreshold)+0.3m(滑窗范围)
    std::cout << "width : " <<roi.GetWidth() << " height : " << roi.GetHeight() << " center : " << roi.GetCenter().GetX() << " " << roi.GetCenter().GetY() << std::endl;

    // 3. compute offset (in meters - lower left corner)
    Vector2<kt_double> offset;  //offset是栅格左下角在世界坐标系中的位置
    offset.SetX(scanPose.GetX() - (0.5 * (roi.GetWidth() - 1) * m_pCorrelationGrid->GetResolution()));
    offset.SetY(scanPose.GetY() - (0.5 * (roi.GetHeight() - 1) * m_pCorrelationGrid->GetResolution()));

    // 4. set offset
    m_pCorrelationGrid->GetCoordinateConverter()->SetOffset(offset);

    ///////////////////////////////////////

    // set up correlation grid
    AddScans(rBaseScans, scanPose.GetPosition());  //获得了Roi里栅格状态  //这个会对扫描点进行处理

    // compute how far to search in each direction   //coarseSearchOffset 是一个坐标
    Vector2<kt_double> searchDimensions(m_pSearchSpaceProbs->GetWidth(), m_pSearchSpaceProbs->GetHeight());
    Vector2<kt_double> coarseSearchOffset(0.5 * (searchDimensions.GetX() - 1) * m_pCorrelationGrid->GetResolution(),
                                          0.5 * (searchDimensions.GetY() - 1) * m_pCorrelationGrid->GetResolution());

    // a coarse search only checks half the cells in each dimension
    Vector2<kt_double> coarseSearchResolution(2 * m_pCorrelationGrid->GetResolution(),
                                              2 * m_pCorrelationGrid->GetResolution());

    // actual scan-matching   //找到了 最好的位姿使得response最大   //这里面进行了三重for循环来判断是否有合适的旋转
    kt_double bestResponse = CorrelateScan(pScan, scanPose, coarseSearchOffset, coarseSearchResolution,
                                           m_pMapper->m_pCoarseSearchAngleOffset->GetValue(),
                                           m_pMapper->m_pCoarseAngleResolution->GetValue(),
                                           doPenalize, rMean, rCovariance, false);

    if (m_pMapper->m_pUseResponseExpansion->GetValue() == true)
    {
      if (math::DoubleEqual(bestResponse, 0.0))
      {
#ifdef KARTO_DEBUG
        std::cout << "Mapper Info: Expanding response search space!" << std::endl;
#endif
        // try and increase search angle offset with 20 degrees and do another match
        kt_double newSearchAngleOffset = m_pMapper->m_pCoarseSearchAngleOffset->GetValue();
        for (kt_int32u i = 0; i < 3; i++)
        {
          newSearchAngleOffset += math::DegreesToRadians(20);

          bestResponse = CorrelateScan(pScan, scanPose, coarseSearchOffset, coarseSearchResolution,
                                       newSearchAngleOffset, m_pMapper->m_pCoarseAngleResolution->GetValue(),
                                       doPenalize, rMean, rCovariance, false);

          if (math::DoubleEqual(bestResponse, 0.0) == false)
          {
            break;
          }
        }

#ifdef KARTO_DEBUG
        if (math::DoubleEqual(bestResponse, 0.0))
        {
          std::cout << "Mapper Warning: Unable to calculate response!" << std::endl;
        }
#endif
      }
    }

    if (doRefineMatch)
    {
      Vector2<kt_double> fineSearchOffset(coarseSearchResolution * 0.5);
      Vector2<kt_double> fineSearchResolution(m_pCorrelationGrid->GetResolution(), m_pCorrelationGrid->GetResolution());
      bestResponse = CorrelateScan(pScan, rMean, fineSearchOffset, fineSearchResolution,
                                   0.5 * m_pMapper->m_pCoarseAngleResolution->GetValue(),
                                   m_pMapper->m_pFineSearchAngleOffset->GetValue(),
                                   doPenalize, rMean, rCovariance, true);  //这里的true会让程序计算角度方差
    }

#ifdef KARTO_DEBUG
    std::cout << "  BEST POSE = " << rMean << " BEST RESPONSE = " << bestResponse << ",  VARIANCE = "
              << rCovariance(0, 0) << ", " << rCovariance(1, 1) << std::endl;
#endif
    assert(math::InRange(rMean.GetHeading(), -KT_PI, KT_PI));

    return bestResponse;
  }

  /**
   * Finds the best pose for the scan centering the search in the correlation grid
   * at the given pose and search in the space by the vector and angular offsets
   * in increments of the given resolutions
   * @param rScan scan to match against correlation grid
   * @param rSearchCenter the center of the search space
   * @param rSearchSpaceOffset searches poses in the area offset by this vector around search center
   * @param rSearchSpaceResolution how fine a granularity to search in the search space
   * @param searchAngleOffset searches poses in the angles offset by this angle around search center
   * @param searchAngleResolution how fine a granularity to search in the angular search space
   * @param doPenalize whether to penalize matches further from the search center
   * @param rMean output parameter of mean (best pose) of match
   * @param rCovariance output parameter of covariance of match
   * @param doingFineMatch whether to do a finer search after coarse search
   * @return strength of response
   */
  kt_double ScanMatcher::CorrelateScan(LocalizedRangeScan* pScan, const Pose2& rSearchCenter,
                                       const Vector2<kt_double>& rSearchSpaceOffset,
                                       const Vector2<kt_double>& rSearchSpaceResolution,
                                       kt_double searchAngleOffset, kt_double searchAngleResolution,
                                       kt_bool doPenalize, Pose2& rMean, Matrix3& rCovariance, kt_bool doingFineMatch)
  {
    assert(searchAngleResolution != 0.0);

    // setup lookup arrays    //这个函数把lookuparray的角度写好了。每一个lookuparry存储了每一个激光数据(总共361/修改之后为180个)在角度滑窗之后的结果，还没有xy的滑窗
    m_pGridLookup->ComputeOffsets(pScan, rSearchCenter.GetHeading(), searchAngleOffset, searchAngleResolution);

    // only initialize probability grid if computing positional covariance (during coarse match)
    if (!doingFineMatch)
    {
      m_pSearchSpaceProbs->Clear();

      // position search grid - finds lower left corner of search grid
      Vector2<kt_double> offset(rSearchCenter.GetPosition() - rSearchSpaceOffset);
      m_pSearchSpaceProbs->GetCoordinateConverter()->SetOffset(offset);
    }

    // calculate position arrays
    //以下计算出需要的各种x，y，angle用于三重循环 ，在这里x有16个值，y有16个值，angle有21个值
    std::vector<kt_double> xPoses;
    kt_int32u nX = static_cast<kt_int32u>(math::Round(rSearchSpaceOffset.GetX() *
                                          2.0 / rSearchSpaceResolution.GetX()) + 1);
    kt_double startX = -rSearchSpaceOffset.GetX();
    for (kt_int32u xIndex = 0; xIndex < nX; xIndex++)
    {
      xPoses.push_back(startX + xIndex * rSearchSpaceResolution.GetX());
    }
    assert(math::DoubleEqual(xPoses.back(), -startX));

    std::vector<kt_double> yPoses;
    kt_int32u nY = static_cast<kt_int32u>(math::Round(rSearchSpaceOffset.GetY() *
                                          2.0 / rSearchSpaceResolution.GetY()) + 1);
    kt_double startY = -rSearchSpaceOffset.GetY();
    for (kt_int32u yIndex = 0; yIndex < nY; yIndex++)
    {
      yPoses.push_back(startY + yIndex * rSearchSpaceResolution.GetY());
    }
    assert(math::DoubleEqual(yPoses.back(), -startY));

    // calculate pose response array size
    kt_int32u nAngles = static_cast<kt_int32u>(math::Round(searchAngleOffset * 2.0 / searchAngleResolution) + 1);

    kt_int32u poseResponseSize = static_cast<kt_int32u>(xPoses.size() * yPoses.size() * nAngles);  // 位置、角度的变化

    // allocate array   //Pose2包含了 x,y,theta. 滑窗的结果存储在pPoseResponse中
    std::pair<kt_double, Pose2>* pPoseResponse = new std::pair<kt_double, Pose2>[poseResponseSize];
    //这边有涉及到grid栅格的index和我想的不一样的问题，并非相对左下角的坐标转换为 index，似乎是中心是0,0
    Vector2<kt_int32s> startGridPoint = m_pCorrelationGrid->WorldToGrid(Vector2<kt_double>(rSearchCenter.GetX()
                                                                        + startX, rSearchCenter.GetY() + startY));

    kt_int32u poseResponseCounter = 0;
    forEachAs(std::vector<kt_double>, &yPoses, yIter)
    {
      kt_double y = *yIter;
      kt_double newPositionY = rSearchCenter.GetY() + y;
      kt_double squareY = math::Square(y);

      forEachAs(std::vector<kt_double>, &xPoses, xIter)
      {
        kt_double x = *xIter;
        kt_double newPositionX = rSearchCenter.GetX() + x;
        kt_double squareX = math::Square(x);

        //由 x,y得出一个 gridIndex
        Vector2<kt_int32s> gridPoint = m_pCorrelationGrid->WorldToGrid(Vector2<kt_double>(newPositionX, newPositionY));  
        kt_int32s gridIndex = m_pCorrelationGrid->GridIndex(gridPoint);
        assert(gridIndex >= 0);

        kt_double angle = 0.0;
        kt_double startAngle = rSearchCenter.GetHeading() - searchAngleOffset;
        for (kt_int32u angleIndex = 0; angleIndex < nAngles; angleIndex++)
        {
          angle = startAngle + angleIndex * searchAngleResolution;
          //由 angleIndex 和 gridIndex得出response   //将旋转平移之后的这一帧数据和上一帧的数据进行匹配得分
          kt_double response = GetResponse(angleIndex, gridIndex);
          if (doPenalize && (math::DoubleEqual(response, 0.0) == false))
          {
            // simple model (approximate Gaussian) to take odometry into account

            kt_double squaredDistance = squareX + squareY;
            kt_double distancePenalty = 1.0 - (DISTANCE_PENALTY_GAIN *
                                               squaredDistance / m_pMapper->m_pDistanceVariancePenalty->GetValue());
            distancePenalty = math::Maximum(distancePenalty, m_pMapper->m_pMinimumDistancePenalty->GetValue());

            kt_double squaredAngleDistance = math::Square(angle - rSearchCenter.GetHeading());
            kt_double anglePenalty = 1.0 - (ANGLE_PENALTY_GAIN *
                                            squaredAngleDistance / m_pMapper->m_pAngleVariancePenalty->GetValue());
            anglePenalty = math::Maximum(anglePenalty, m_pMapper->m_pMinimumAnglePenalty->GetValue());

            response *= (distancePenalty * anglePenalty);
          }

          // store response and pose
          pPoseResponse[poseResponseCounter] = std::pair<kt_double, Pose2>(response, Pose2(newPositionX, newPositionY,
                                                                           math::NormalizeAngle(angle)));
          poseResponseCounter++;
        }

        assert(math::DoubleEqual(angle, rSearchCenter.GetHeading() + searchAngleOffset));
      }
    }

    assert(poseResponseSize == poseResponseCounter);

    // find value of best response (in [0; 1])
    kt_double bestResponse = -1;
    for (kt_int32u i = 0; i < poseResponseSize; i++)
    {
      bestResponse = math::Maximum(bestResponse, pPoseResponse[i].first);

      // will compute positional covariance, save best relative probability for each cell
      if (!doingFineMatch)
      {
        const Pose2& rPose = pPoseResponse[i].second;
        Vector2<kt_int32s> grid = m_pSearchSpaceProbs->WorldToGrid(rPose.GetPosition());

        // Changed (kt_double*) to the reinterpret_cast - Luc
        kt_double* ptr = reinterpret_cast<kt_double*>(m_pSearchSpaceProbs->GetDataPointer(grid));
        if (ptr == NULL)
        {
          throw std::runtime_error("Mapper FATAL ERROR - Index out of range in probability search!");
        }

        *ptr = math::Maximum(pPoseResponse[i].first, *ptr);
      }
    }

    // average all poses with same highest response   //最大的响应值对应的位姿可能有很多种
    Vector2<kt_double> averagePosition;
    kt_double thetaX = 0.0;
    kt_double thetaY = 0.0;
    kt_int32s averagePoseCount = 0;
    for (kt_int32u i = 0; i < poseResponseSize; i++)
    {
      if (math::DoubleEqual(pPoseResponse[i].first, bestResponse))
      {
        averagePosition += pPoseResponse[i].second.GetPosition();

        kt_double heading = pPoseResponse[i].second.GetHeading();
        thetaX += cos(heading);
        thetaY += sin(heading);

        averagePoseCount++;                        //对 x,y进行平均
      }
    }

    Pose2 averagePose;
    if (averagePoseCount > 0)
    {
      averagePosition /= averagePoseCount;

      thetaX /= averagePoseCount;
      thetaY /= averagePoseCount;

      averagePose = Pose2(averagePosition, atan2(thetaY, thetaX));
    }
    else
    {
      throw std::runtime_error("Mapper FATAL ERROR - Unable to find best position");
    }

    // delete pose response array
    delete [] pPoseResponse;

#ifdef KARTO_DEBUG
    std::cout << "bestPose: " << averagePose << std::endl;
    std::cout << "bestResponse: " << bestResponse << std::endl;
#endif

    if (!doingFineMatch)
    {  //计算协方差
      ComputePositionalCovariance(averagePose, bestResponse, rSearchCenter, rSearchSpaceOffset,
                                  rSearchSpaceResolution, searchAngleResolution, rCovariance);
    }
    else
    {
      ComputeAngularCovariance(averagePose, bestResponse, rSearchCenter,
                              searchAngleOffset, searchAngleResolution, rCovariance);
    }

    rMean = averagePose;

#ifdef KARTO_DEBUG
    std::cout << "bestPose: " << averagePose << std::endl;
#endif

    if (bestResponse > 1.0)
    {
      bestResponse = 1.0;
    }

    assert(math::InRange(bestResponse, 0.0, 1.0));
    assert(math::InRange(rMean.GetHeading(), -KT_PI, KT_PI));

    return bestResponse;
  }

  /**
   * Computes the positional covariance of the best pose
   * @param rBestPose
   * @param bestResponse
   * @param rSearchCenter
   * @param rSearchSpaceOffset
   * @param rSearchSpaceResolution
   * @param searchAngleResolution
   * @param rCovariance
   */
  void ScanMatcher::ComputePositionalCovariance(const Pose2& rBestPose, kt_double bestResponse,
                                                const Pose2& rSearchCenter,
                                                const Vector2<kt_double>& rSearchSpaceOffset,
                                                const Vector2<kt_double>& rSearchSpaceResolution,
                                                kt_double searchAngleResolution, Matrix3& rCovariance)
  {
    // reset covariance to identity matrix
    rCovariance.SetToIdentity();

    // if best response is vary small return max variance
    if (bestResponse < KT_TOLERANCE)
    {
      rCovariance(0, 0) = MAX_VARIANCE;  // XX
      rCovariance(1, 1) = MAX_VARIANCE;  // YY
      rCovariance(2, 2) = 4 * math::Square(searchAngleResolution);  // TH*TH

      return;
    }

    kt_double accumulatedVarianceXX = 0;
    kt_double accumulatedVarianceXY = 0;
    kt_double accumulatedVarianceYY = 0;
    kt_double norm = 0;

    kt_double dx = rBestPose.GetX() - rSearchCenter.GetX();
    kt_double dy = rBestPose.GetY() - rSearchCenter.GetY();

    kt_double offsetX = rSearchSpaceOffset.GetX();
    kt_double offsetY = rSearchSpaceOffset.GetY();

    kt_int32u nX = static_cast<kt_int32u>(math::Round(offsetX * 2.0 / rSearchSpaceResolution.GetX()) + 1);
    kt_double startX = -offsetX;
    assert(math::DoubleEqual(startX + (nX - 1) * rSearchSpaceResolution.GetX(), -startX));

    kt_int32u nY = static_cast<kt_int32u>(math::Round(offsetY * 2.0 / rSearchSpaceResolution.GetY()) + 1);
    kt_double startY = -offsetY;
    assert(math::DoubleEqual(startY + (nY - 1) * rSearchSpaceResolution.GetY(), -startY));

    for (kt_int32u yIndex = 0; yIndex < nY; yIndex++)
    {
      kt_double y = startY + yIndex * rSearchSpaceResolution.GetY();

      for (kt_int32u xIndex = 0; xIndex < nX; xIndex++)
      {
        kt_double x = startX + xIndex * rSearchSpaceResolution.GetX();

        Vector2<kt_int32s> gridPoint = m_pSearchSpaceProbs->WorldToGrid(Vector2<kt_double>(rSearchCenter.GetX() + x,
                                                                                           rSearchCenter.GetY() + y));
        kt_double response = *(m_pSearchSpaceProbs->GetDataPointer(gridPoint));

        // response is not a low response
        if (response >= (bestResponse - 0.1))
        {
          norm += response;
          accumulatedVarianceXX += (math::Square(x - dx) * response);
          accumulatedVarianceXY += ((x - dx) * (y - dy) * response);
          accumulatedVarianceYY += (math::Square(y - dy) * response);
        }
      }
    }

    if (norm > KT_TOLERANCE)
    {
      kt_double varianceXX = accumulatedVarianceXX / norm;
      kt_double varianceXY = accumulatedVarianceXY / norm;
      kt_double varianceYY = accumulatedVarianceYY / norm;
      kt_double varianceTHTH = 4 * math::Square(searchAngleResolution);

      // lower-bound variances so that they are not too small;
      // ensures that links are not too tight
      kt_double minVarianceXX = 0.1 * math::Square(rSearchSpaceResolution.GetX());
      kt_double minVarianceYY = 0.1 * math::Square(rSearchSpaceResolution.GetY());
      varianceXX = math::Maximum(varianceXX, minVarianceXX);
      varianceYY = math::Maximum(varianceYY, minVarianceYY);

      // increase variance for poorer responses
      kt_double multiplier = 1.0 / bestResponse;
      rCovariance(0, 0) = varianceXX * multiplier;
      rCovariance(0, 1) = varianceXY * multiplier;
      rCovariance(1, 0) = varianceXY * multiplier;
      rCovariance(1, 1) = varianceYY * multiplier;
      rCovariance(2, 2) = varianceTHTH;  // this value will be set in ComputeAngularCovariance
    }

    // if values are 0, set to MAX_VARIANCE
    // values might be 0 if points are too sparse and thus don't hit other points
    if (math::DoubleEqual(rCovariance(0, 0), 0.0))
    {
      rCovariance(0, 0) = MAX_VARIANCE;
    }

    if (math::DoubleEqual(rCovariance(1, 1), 0.0))
    {
      rCovariance(1, 1) = MAX_VARIANCE;
    }
  }

  /**
   * Computes the angular covariance of the best pose
   * @param rBestPose
   * @param bestResponse
   * @param rSearchCenter
   * @param rSearchAngleOffset
   * @param searchAngleResolution
   * @param rCovariance
   */
  void ScanMatcher::ComputeAngularCovariance(const Pose2& rBestPose,
                                             kt_double bestResponse,
                                             const Pose2& rSearchCenter,
                                             kt_double searchAngleOffset,
                                             kt_double searchAngleResolution,
                                             Matrix3& rCovariance)
  {
    // NOTE: do not reset covariance matrix

    // normalize angle difference
    kt_double bestAngle = math::NormalizeAngleDifference(rBestPose.GetHeading(), rSearchCenter.GetHeading());

    Vector2<kt_int32s> gridPoint = m_pCorrelationGrid->WorldToGrid(rBestPose.GetPosition());
    kt_int32s gridIndex = m_pCorrelationGrid->GridIndex(gridPoint);

    kt_int32u nAngles = static_cast<kt_int32u>(math::Round(searchAngleOffset * 2 / searchAngleResolution) + 1);

    kt_double angle = 0.0;
    kt_double startAngle = rSearchCenter.GetHeading() - searchAngleOffset;

    kt_double norm = 0.0;
    kt_double accumulatedVarianceThTh = 0.0;
    for (kt_int32u angleIndex = 0; angleIndex < nAngles; angleIndex++)
    {
      angle = startAngle + angleIndex * searchAngleResolution;
      kt_double response = GetResponse(angleIndex, gridIndex);

      // response is not a low response
      if (response >= (bestResponse - 0.1))
      {
        norm += response;
        accumulatedVarianceThTh += (math::Square(angle - bestAngle) * response);
      }
    }
    assert(math::DoubleEqual(angle, rSearchCenter.GetHeading() + searchAngleOffset));

    if (norm > KT_TOLERANCE)
    {
      if (accumulatedVarianceThTh < KT_TOLERANCE)
      {
        accumulatedVarianceThTh = math::Square(searchAngleResolution);
      }

      accumulatedVarianceThTh /= norm;
    }
    else
    {
      accumulatedVarianceThTh = 1000 * math::Square(searchAngleResolution);
    }

    rCovariance(2, 2) = accumulatedVarianceThTh;
  }

  /**
   * Marks cells where scans' points hit as being occupied
   * @param rScans scans whose points will mark cells in grid as being occupied
   * @param viewPoint do not add points that belong to scans "opposite" the view point
   */
  void ScanMatcher::AddScans(const LocalizedRangeScanVector& rScans, Vector2<kt_double> viewPoint)
  {
    m_pCorrelationGrid->Clear();   //把栅格地图的所有值(0,100,255)置为0

    // add all scans to grid
    const_forEach(LocalizedRangeScanVector, &rScans)   // rScans 可能是 running scans 或者 相邻的scans或者 闭环产生的scans
    {
      AddScan(*iter, viewPoint);    //viewPoint 是当前帧的 在世界坐标系中的位置，但是不包含laser朝向信息，而 *iter都是存储的之前的帧的扫描点在世界坐标系中的位置
    }
  }

  /**
   * Marks cells where scans' points hit as being occupied.  Can smear points as they are added.
   * @param pScan scan whose points will mark cells in grid as being occupied
   * @param viewPoint do not add points that belong to scans "opposite" the view point
   * @param doSmear whether the points will be smeared
   */
  void ScanMatcher::AddScan(LocalizedRangeScan* pScan, const Vector2<kt_double>& rViewPoint, kt_bool doSmear)
  {
    PointVectorDouble validPoints = FindValidPoints(pScan, rViewPoint);   //找到之前的scan在 rviewpoint看来合适的点，具体条件见函数内部，记为有效的点

    // put in all valid points
    const_forEach(PointVectorDouble, &validPoints)
    {
      Vector2<kt_int32s> gridPoint = m_pCorrelationGrid->WorldToGrid(*iter);  //把 point的位置变为 在栅格地图上的坐标，栅格地图的原点是左下角
      if (!math::IsUpTo(gridPoint.GetX(), m_pCorrelationGrid->GetROI().GetWidth()) ||
          !math::IsUpTo(gridPoint.GetY(), m_pCorrelationGrid->GetROI().GetHeight()))
      {
        // point not in grid
        continue;
      }

      int gridIndex = m_pCorrelationGrid->GridIndex(gridPoint);   //二维变为一维索引

      // set grid cell as occupied
      if (m_pCorrelationGrid->GetDataPointer()[gridIndex] == GridStates_Occupied)  //查询grid cell状态
      {
        // value already set
        continue;
      }

      m_pCorrelationGrid->GetDataPointer()[gridIndex] = GridStates_Occupied;  //在这里将之前的数据帧的所有扫描点都在grid中更新状态，用于之后的匹配

      // smear grid
      if (doSmear == true)
      {
        m_pCorrelationGrid->SmearPoint(gridPoint);
      }
    }
  }

  /**
   * Compute which points in a scan are on the same side as the given viewpoint
   * @param pScan
   * @param rViewPoint
   * @return points on the same side
   */
  PointVectorDouble ScanMatcher::FindValidPoints(LocalizedRangeScan* pScan, const Vector2<kt_double>& rViewPoint) const
  {
    const PointVectorDouble& rPointReadings = pScan->GetPointReadings();

    // points must be at least 10 cm away when making comparisons of inside/outside of viewpoint
    const kt_double minSquareDistance = math::Square(0.1);  // in m^2

    // this iterator lags from the main iterator adding points only when the points are on
    // the same side as the viewpoint
    PointVectorDouble::const_iterator trailingPointIter = rPointReadings.begin();
    PointVectorDouble validPoints;

    Vector2<kt_double> firstPoint;
    kt_bool firstTime = true;
    const_forEach(PointVectorDouble, &rPointReadings)  //访问一个帧的扫描点数据，这些数据的含义是扫描点在世界坐标系中的位置
    {
      Vector2<kt_double> currentPoint = *iter;

      if (firstTime && !std::isnan(currentPoint.GetX()) && !std::isnan(currentPoint.GetY()))
      {
        firstPoint = currentPoint;
        firstTime = false;
      }

      Vector2<kt_double> delta = firstPoint - currentPoint;
      if (delta.SquaredLength() > minSquareDistance)
      {
        // This compute the Determinant (viewPoint FirstPoint, viewPoint currentPoint)
        // Which computes the direction of rotation, if the rotation is counterclock
        // wise then we are looking at data we should keep. If it's negative rotation
        // we should not included in in the matching
        // have enough distance, check viewpoint
        double a = rViewPoint.GetY() - firstPoint.GetY();
        double b = firstPoint.GetX() - rViewPoint.GetX();
        double c = firstPoint.GetY() * rViewPoint.GetX() - firstPoint.GetX() * rViewPoint.GetY();
        double ss = currentPoint.GetX() * a + currentPoint.GetY() * b + c;

        // reset beginning point
        firstPoint = currentPoint;

        if (ss < 0.0)  // wrong side, skip and keep going
        {
          trailingPointIter = iter;
        }
        else
        {
          for (; trailingPointIter != iter; ++trailingPointIter)
          {
            validPoints.push_back(*trailingPointIter);    //将符合条件的（1.距离大于一定值2.是正确的时针方向）扫描点加入validPoints之中
          }
        }
      }
    }

    return validPoints;
  }

  /**
   * Get response at given position for given rotation (only look up valid points)
   * @param angleIndex
   * @param gridPositionIndex
   * @return response
   */
  kt_double ScanMatcher::GetResponse(kt_int32u angleIndex, kt_int32s gridPositionIndex) const
  {
    kt_double response = 0.0;

    // add up value for each point
    kt_int8u* pByte = m_pCorrelationGrid->GetDataPointer() + gridPositionIndex;   //实际获得了Grid<T>的m_pData， T* m_pData;  即 uchar* m_pData; 大小是m_WidthStep * m_Height，因此是栅格格子个数大小
    //m_pGridLoopup存储 nAngles个LookupArray指针，每一个称之为pOffsets，每一个offsets存储了每一个扫描点的数据。这个数据应为栅格是否被占据的状态
    const LookupArray* pOffsets = m_pGridLookup->GetLookupArray(angleIndex); //LookupArray* pOffsets的m_pArray中存储了总共n个扫描点的数据
    assert(pOffsets != NULL);

    // get number of points in offset list
    kt_int32u nPoints = pOffsets->GetSize();
    if (nPoints == 0)
    {
      return response;
    }

    // calculate response
    kt_int32s* pAngleIndexPointer = pOffsets->GetArrayPointer();
    for (kt_int32u i = 0; i < nPoints; i++)
    {
      //将当前帧的扫描点(nPoints个)经过旋选之后的结果存储起来了，存在哪里了呢？存在了m_pGridLookup里面。存储的表示是pAngleIndexPointer[i]的id，表示存在了格子的第几个上面
      // ignore points that fall off the grid
      kt_int32s pointGridIndex = gridPositionIndex + pAngleIndexPointer[i];
      if (!math::IsUpTo(pointGridIndex, m_pCorrelationGrid->GetDataSize()) || pAngleIndexPointer[i] == INVALID_SCAN)
      {
        continue;
      }
      //那么如果pByte是一个已经写好之前的帧的扫描点的占据状态位置的话，就只需要访问一下当前帧当前旋转角度当前扫描点的落脚点所在的格子的响应值就好了。
      //如果以前被占据了，就是100，如果不是，就应该是0. 
      //如果还有疑惑，就应该是pByte所代表的数据是如何赋值的。只需要找到这部分代码就可以了。
      // uses index offsets to efficiently find location of point in the grid
      response += pByte[pAngleIndexPointer[i]];
    }

    // normalize response
    response /= (nPoints * GridStates_Occupied);
    assert(fabs(response) <= 1.0);

    return response;
  }


  ////////////////////////////////////////////////////////////////////////////////////////
  ////////////////////////////////////////////////////////////////////////////////////////
  ////////////////////////////////////////////////////////////////////////////////////////
  // 深度优先（DFS）广度优先（BFS）算法可以参考这篇文章（http://www.cnblogs.com/skywang12345/p/3711483.html）
  template<typename T>   
  class BreadthFirstTraversal : public GraphTraversal<T>
  {
  public:
    /**
     * Constructs a breadth-first traverser for the given graph
     */
    BreadthFirstTraversal(Graph<T>* pGraph)
    : GraphTraversal<T>(pGraph)
    {
    }

    /**
     * Destructor
     */
    virtual ~BreadthFirstTraversal()
    {
    }

  public:
    /**
     * Traverse the graph starting with the given vertex; applies the visitor to visited nodes
     * @param pStartVertex
     * @param pVisitor
     * @return visited vertices
     */
    virtual std::vector<T*> Traverse(Vertex<T>* pStartVertex, Visitor<T>* pVisitor)
    {
      std::queue<Vertex<T>*> toVisit;
      std::set<Vertex<T>*> seenVertices;
      std::vector<Vertex<T>*> validVertices;

      toVisit.push(pStartVertex);
      seenVertices.insert(pStartVertex);

      do
      {
        Vertex<T>* pNext = toVisit.front();
        toVisit.pop();

        if (pVisitor->Visit(pNext))     //访问第一个节点，如果在距离阈值之内
        {
          // vertex is valid, explore neighbors
          validVertices.push_back(pNext);

          std::vector<Vertex<T>*> adjacentVertices = pNext->GetAdjacentVertices();  //之前成功认为这一帧好，现在就要得到这一帧的相邻边
          forEach(typename std::vector<Vertex<T>*>, &adjacentVertices)
          {
            Vertex<T>* pAdjacent = *iter;

            // adjacent vertex has not yet been seen, add to queue for processing
            if (seenVertices.find(pAdjacent) == seenVertices.end())  //如果没找到，也就是没有访问过
            {
              toVisit.push(pAdjacent);    //通过这个push操作使得queue不断增加数据，直到所有连接关系被访问过，最终使用validVertices来作为相邻结果
              seenVertices.insert(pAdjacent);
            }
          }
        }
      } while (toVisit.empty() == false);

      std::vector<T*> objects;
      forEach(typename std::vector<Vertex<T>*>, &validVertices)
      {
        objects.push_back((*iter)->GetObject());
      }  

      return objects; //最终使用validVertices来作为相邻结果
    }
  };  // class BreadthFirstTraversal

  ////////////////////////////////////////////////////////////////////////////////////////
  ////////////////////////////////////////////////////////////////////////////////////////
  ////////////////////////////////////////////////////////////////////////////////////////
  //很简单的class，第一个功能是初始化一个参照的当前帧，以及比较距离的阈值； 第二个功能是根据当前帧和阈值实现 与 指定 pVertex的数据的barycenter的平方距离计算，判断是否相邻
  class NearScanVisitor : public Visitor<LocalizedRangeScan>
  {
  public:
    NearScanVisitor(LocalizedRangeScan* pScan, kt_double maxDistance, kt_bool useScanBarycenter)
      : m_MaxDistanceSquared(math::Square(maxDistance))
      , m_UseScanBarycenter(useScanBarycenter)
    {
      //m_CenterPose就是 Barycenter
      m_CenterPose = pScan->GetReferencePose(m_UseScanBarycenter);
    }

    virtual kt_bool Visit(Vertex<LocalizedRangeScan>* pVertex)
    {
      LocalizedRangeScan* pScan = pVertex->GetObject();

      Pose2 pose = pScan->GetReferencePose(m_UseScanBarycenter);
      // m_CenterPose存储了当前帧的 Barycenter， 而pose则是 pVertex中的帧的Barycenter，于是下面squareDistance就是在比较这两个数值的平方距离
      kt_double squaredDistance = pose.GetPosition().SquaredDistance(m_CenterPose.GetPosition());
      return (squaredDistance <= m_MaxDistanceSquared - KT_TOLERANCE);
    }

  protected:
    Pose2 m_CenterPose;
    kt_double m_MaxDistanceSquared;
    kt_bool m_UseScanBarycenter;
  };  // NearScanVisitor

  ////////////////////////////////////////////////////////////////////////////////////////
  ////////////////////////////////////////////////////////////////////////////////////////
  ////////////////////////////////////////////////////////////////////////////////////////

  MapperGraph::MapperGraph(Mapper* pMapper, kt_double rangeThreshold)
    : m_pMapper(pMapper)
  {
    m_pLoopScanMatcher = ScanMatcher::Create(pMapper, m_pMapper->m_pLoopSearchSpaceDimension->GetValue(),
                                             m_pMapper->m_pLoopSearchSpaceResolution->GetValue(),
                                             m_pMapper->m_pLoopSearchSpaceSmearDeviation->GetValue(), rangeThreshold);
    assert(m_pLoopScanMatcher);

    m_pTraversal = new BreadthFirstTraversal<LocalizedRangeScan>(this);
  }

  MapperGraph::~MapperGraph()
  {
    delete m_pLoopScanMatcher;
    m_pLoopScanMatcher = NULL;

    delete m_pTraversal;
    m_pTraversal = NULL;
  }

  void MapperGraph::AddVertex(LocalizedRangeScan* pScan)
  {
    assert(pScan);

    if (pScan != NULL)
    {
      Vertex<LocalizedRangeScan>* pVertex = new Vertex<LocalizedRangeScan>(pScan);
      Graph<LocalizedRangeScan>::AddVertex(pScan->GetSensorName(), pVertex);
      if (m_pMapper->m_pScanOptimizer != NULL)
      {
        m_pMapper->m_pScanOptimizer->AddNode(pVertex);
      }
    }
  }

  void MapperGraph::AddEdges(LocalizedRangeScan* pScan, const Matrix3& rCovariance)
  {
    MapperSensorManager* pSensorManager = m_pMapper->m_pMapperSensorManager;

    const Name& rSensorName = pScan->GetSensorName();

    // link to previous scan
    kt_int32s previousScanNum = pScan->GetStateId() - 1;
    if (pSensorManager->GetLastScan(rSensorName) != NULL)   //添加边的第一种方式，总共有三种方式
    {
      assert(previousScanNum >= 0);
      LinkScans(pSensorManager->GetScan(rSensorName, previousScanNum), pScan, pScan->GetSensorPose(), rCovariance);
    }

    Pose2Vector means;
    std::vector<Matrix3> covariances;

    // first scan (link to first scan of other robots)
    if (pSensorManager->GetLastScan(rSensorName) == NULL)
    {
      assert(pSensorManager->GetScans(rSensorName).size() == 1);

      std::vector<Name> deviceNames = pSensorManager->GetSensorNames();
      forEach(std::vector<Name>, &deviceNames)
      {
        const Name& rCandidateSensorName = *iter;

        // skip if candidate device is the same or other device has no scans
        if ((rCandidateSensorName == rSensorName) || (pSensorManager->GetScans(rCandidateSensorName).empty()))
        { 
          continue;
        }

        Pose2 bestPose;
        Matrix3 covariance;
        kt_double response = m_pMapper->m_pSequentialScanMatcher->MatchScan(pScan,
                                                                  pSensorManager->GetScans(rCandidateSensorName),
                                                                  bestPose, covariance);
        LinkScans(pScan, pSensorManager->GetScan(rCandidateSensorName, 0), bestPose, covariance);  //第二种添加边的方式

        // only add to means and covariances if response was high "enough"
        if (response > m_pMapper->m_pLinkMatchMinimumResponseFine->GetValue())
        {
          means.push_back(bestPose);
          covariances.push_back(covariance);
        }
      }
    }
    else
    {
      // link to running scans
      Pose2 scanPose = pScan->GetSensorPose();
      means.push_back(scanPose);
      covariances.push_back(rCovariance);
      LinkChainToScan(pSensorManager->GetRunningScans(rSensorName), pScan, scanPose, rCovariance);  //第三种添加边的方式
    }

    // link to other near chains (chains that include new scan are invalid)
    LinkNearChains(pScan, means, covariances);

    if (!means.empty())
    {
      pScan->SetSensorPose(ComputeWeightedMean(means, covariances));
    }
  }

  kt_bool MapperGraph::TryCloseLoop(LocalizedRangeScan* pScan, const Name& rSensorName)
  {
    kt_bool loopClosed = false;

    kt_int32u scanIndex = 0;
    //通过这个FindPossibleLoopClosure先找到临近的10帧，要保证它们和当前帧barycenter距离小于4m，同时是连续的10帧，同时10帧之中不包含当前帧的相邻帧（相邻帧的获取在AddEdges函数中）
    LocalizedRangeScanVector candidateChain = FindPossibleLoopClosure(pScan, rSensorName, scanIndex);

    while (!candidateChain.empty())
    {
      Pose2 bestPose;
      Matrix3 covariance;
      kt_double coarseResponse = m_pLoopScanMatcher->MatchScan(pScan, candidateChain,
                                                               bestPose, covariance, false, false);

      std::stringstream stream;
      stream << "COARSE RESPONSE: " << coarseResponse
             << " (> " << m_pMapper->m_pLoopMatchMinimumResponseCoarse->GetValue() << ")"
             << std::endl;
      stream << "            var: " << covariance(0, 0) << ",  " << covariance(1, 1)
             << " (< " << m_pMapper->m_pLoopMatchMaximumVarianceCoarse->GetValue() << ")";

      m_pMapper->FireLoopClosureCheck(stream.str());

      if ((coarseResponse > m_pMapper->m_pLoopMatchMinimumResponseCoarse->GetValue()) &&
          (covariance(0, 0) < m_pMapper->m_pLoopMatchMaximumVarianceCoarse->GetValue()) &&
          (covariance(1, 1) < m_pMapper->m_pLoopMatchMaximumVarianceCoarse->GetValue()))
      {
        LocalizedRangeScan tmpScan(pScan->GetSensorName(), pScan->GetRangeReadingsVector());
        tmpScan.SetUniqueId(pScan->GetUniqueId());
        tmpScan.SetTime(pScan->GetTime());
        tmpScan.SetStateId(pScan->GetStateId());
        tmpScan.SetCorrectedPose(pScan->GetCorrectedPose());
        tmpScan.SetSensorPose(bestPose);  // This also updates OdometricPose.
        kt_double fineResponse = m_pMapper->m_pSequentialScanMatcher->MatchScan(&tmpScan, candidateChain,
                                                                                bestPose, covariance, false);

        std::stringstream stream1;
        stream1 << "FINE RESPONSE: " << fineResponse << " (>"
                << m_pMapper->m_pLoopMatchMinimumResponseFine->GetValue() << ")" << std::endl;
        m_pMapper->FireLoopClosureCheck(stream1.str());

        if (fineResponse < m_pMapper->m_pLoopMatchMinimumResponseFine->GetValue())
        {
          m_pMapper->FireLoopClosureCheck("REJECTED!");
        }
        else
        {
          m_pMapper->FireBeginLoopClosure("Closing loop...");

          pScan->SetSensorPose(bestPose);
          LinkChainToScan(candidateChain, pScan, bestPose, covariance);
          CorrectPoses();

          m_pMapper->FireEndLoopClosure("Loop closed!");

          loopClosed = true;
        }
      }

      candidateChain = FindPossibleLoopClosure(pScan, rSensorName, scanIndex);
    }

    return loopClosed;
  }

  LocalizedRangeScan* MapperGraph::GetClosestScanToPose(const LocalizedRangeScanVector& rScans,
                                                        const Pose2& rPose) const
  {
    LocalizedRangeScan* pClosestScan = NULL;
    kt_double bestSquaredDistance = DBL_MAX;

    const_forEach(LocalizedRangeScanVector, &rScans)
    {
      Pose2 scanPose = (*iter)->GetReferencePose(m_pMapper->m_pUseScanBarycenter->GetValue());

      kt_double squaredDistance = rPose.GetPosition().SquaredDistance(scanPose.GetPosition());
      if (squaredDistance < bestSquaredDistance)
      {
        bestSquaredDistance = squaredDistance;
        pClosestScan = *iter;
      }
    }

    return pClosestScan;
  }

  Edge<LocalizedRangeScan>* MapperGraph::AddEdge(LocalizedRangeScan* pSourceScan,
                                                 LocalizedRangeScan* pTargetScan, kt_bool& rIsNewEdge)
  {
    // check that vertex exists
    assert(pSourceScan->GetStateId() < (kt_int32s)m_Vertices[pSourceScan->GetSensorName()].size());
    assert(pTargetScan->GetStateId() < (kt_int32s)m_Vertices[pTargetScan->GetSensorName()].size());

    Vertex<LocalizedRangeScan>* v1 = m_Vertices[pSourceScan->GetSensorName()][pSourceScan->GetStateId()];
    Vertex<LocalizedRangeScan>* v2 = m_Vertices[pTargetScan->GetSensorName()][pTargetScan->GetStateId()];

    // see if edge already exists
    const_forEach(std::vector<Edge<LocalizedRangeScan>*>, &(v1->GetEdges()))
    {
      Edge<LocalizedRangeScan>* pEdge = *iter;

      if (pEdge->GetTarget() == v2)
      {
        rIsNewEdge = false;
        return pEdge;
      }
    }

    Edge<LocalizedRangeScan>* pEdge = new Edge<LocalizedRangeScan>(v1, v2);
    Graph<LocalizedRangeScan>::AddEdge(pEdge);
    rIsNewEdge = true;
    return pEdge;
  }

  void MapperGraph::LinkScans(LocalizedRangeScan* pFromScan, LocalizedRangeScan* pToScan,
                              const Pose2& rMean, const Matrix3& rCovariance)
  {
    kt_bool isNewEdge = true;
    //将source与target连接起来，或者说将from与to连接起来
    Edge<LocalizedRangeScan>* pEdge = AddEdge(pFromScan, pToScan, isNewEdge);
    //利用ScanOptimizer来优化这条边，输入的是边，误差，初始化值，可信度
    // only attach link information if the edge is new
    if (isNewEdge == true)
    {
      pEdge->SetLabel(new LinkInfo(pFromScan->GetSensorPose(), rMean, rCovariance));
      if (m_pMapper->m_pScanOptimizer != NULL)
      {
        m_pMapper->m_pScanOptimizer->AddConstraint(pEdge);
      }
    }
  }

  void MapperGraph::LinkNearChains(LocalizedRangeScan* pScan, Pose2Vector& rMeans, std::vector<Matrix3>& rCovariances)
  {
    //查找相邻帧的操作只能理解前部分，也就是使用深度优先搜索的方法访问，后面还有一些判断条件，不明所以
    const std::vector<LocalizedRangeScanVector> nearChains = FindNearChains(pScan);
    const_forEach(std::vector<LocalizedRangeScanVector>, &nearChains)   //这是一个vector<vector<LocalizedRangeScan>>
    {
      if (iter->size() < m_pMapper->m_pLoopMatchMinimumChainSize->GetValue())
      {
        continue;
      }

      Pose2 mean;
      Matrix3 covariance;
      // match scan against "near" chain
      kt_double response = m_pMapper->m_pSequentialScanMatcher->MatchScan(pScan, *iter, mean, covariance, false);
      if (response > m_pMapper->m_pLinkMatchMinimumResponseFine->GetValue() - KT_TOLERANCE)
      {
        rMeans.push_back(mean);
        rCovariances.push_back(covariance);
        LinkChainToScan(*iter, pScan, mean, covariance);
      }
    }
  }

  void MapperGraph::LinkChainToScan(const LocalizedRangeScanVector& rChain, LocalizedRangeScan* pScan,
                                    const Pose2& rMean, const Matrix3& rCovariance)
  {
    Pose2 pose = pScan->GetReferencePose(m_pMapper->m_pUseScanBarycenter->GetValue());
    //找到runningScans中距离自己最近的那个
    LocalizedRangeScan* pClosestScan = GetClosestScanToPose(rChain, pose);
    assert(pClosestScan != NULL);

    Pose2 closestScanPose = pClosestScan->GetReferencePose(m_pMapper->m_pUseScanBarycenter->GetValue());

    kt_double squaredDistance = pose.GetPosition().SquaredDistance(closestScanPose.GetPosition());
    if (squaredDistance < math::Square(m_pMapper->m_pLinkScanMaximumDistance->GetValue()) + KT_TOLERANCE)
    {
      LinkScans(pClosestScan, pScan, rMean, rCovariance);   //连接合适的runningScans中的距离最近的帧
    }
  }
  //前面的操作懂了，但是后面的操作看不太懂，感觉像是多余的代码
  std::vector<LocalizedRangeScanVector> MapperGraph::FindNearChains(LocalizedRangeScan* pScan)
  {
    std::vector<LocalizedRangeScanVector> nearChains;
    //这里得到了 Barycenter，这个当前帧读到的数据的世界坐标系的平均值
    Pose2 scanPose = pScan->GetReferencePose(m_pMapper->m_pUseScanBarycenter->GetValue());

    // to keep track of which scans have been added to a chain
    LocalizedRangeScanVector processed;
    //m_pMapper->m_pLinkScanMaximumDistance->GetValue()值是10，是说要在10m内找linkscan
    //通过深度优先算法实现了查找相邻pscan
    const LocalizedRangeScanVector nearLinkedScans = FindNearLinkedScans(pScan,
                                                     m_pMapper->m_pLinkScanMaximumDistance->GetValue());
    const_forEach(LocalizedRangeScanVector, &nearLinkedScans)
    {
      LocalizedRangeScan* pNearScan = *iter;

      if (pNearScan == pScan)
      {
        continue;
      }

      // scan has already been processed, skip 
      if (find(processed.begin(), processed.end(), pNearScan) != processed.end())
      {
        continue;
      }

      processed.push_back(pNearScan);

      // build up chain
      kt_bool isValidChain = true;
      std::list<LocalizedRangeScan*> chain;

      // add scans before current scan being processed
      for (kt_int32s candidateScanNum = pNearScan->GetStateId() - 1; candidateScanNum >= 0; candidateScanNum--)
      {
        LocalizedRangeScan* pCandidateScan = m_pMapper->m_pMapperSensorManager->GetScan(pNearScan->GetSensorName(),
                                                                                        candidateScanNum);

        // chain is invalid--contains scan being added
        if (pCandidateScan == pScan)
        {
          isValidChain = false;
        }

        Pose2 candidatePose = pCandidateScan->GetReferencePose(m_pMapper->m_pUseScanBarycenter->GetValue());
        kt_double squaredDistance = scanPose.GetPosition().SquaredDistance(candidatePose.GetPosition());

        if (squaredDistance < math::Square(m_pMapper->m_pLinkScanMaximumDistance->GetValue()) + KT_TOLERANCE)
        {
          chain.push_front(pCandidateScan);
          processed.push_back(pCandidateScan);
        }
        else
        {
          break;
        }
      }

      chain.push_back(pNearScan);

      // add scans after current scan being processed
      kt_int32u end = static_cast<kt_int32u>(m_pMapper->m_pMapperSensorManager->GetScans(pNearScan->GetSensorName()).size());
      for (kt_int32u candidateScanNum = pNearScan->GetStateId() + 1; candidateScanNum < end; candidateScanNum++)
      {
        LocalizedRangeScan* pCandidateScan = m_pMapper->m_pMapperSensorManager->GetScan(pNearScan->GetSensorName(),
                                                                                        candidateScanNum);

        if (pCandidateScan == pScan)
        {
          isValidChain = false;
        }

        Pose2 candidatePose = pCandidateScan->GetReferencePose(m_pMapper->m_pUseScanBarycenter->GetValue());;
        kt_double squaredDistance = scanPose.GetPosition().SquaredDistance(candidatePose.GetPosition());

        if (squaredDistance < math::Square(m_pMapper->m_pLinkScanMaximumDistance->GetValue()) + KT_TOLERANCE)
        {
          chain.push_back(pCandidateScan);
          processed.push_back(pCandidateScan);
        }
        else
        {
          break;
        }
      }

      if (isValidChain)
      {
        // change list to vector
        LocalizedRangeScanVector tempChain;
        std::copy(chain.begin(), chain.end(), std::inserter(tempChain, tempChain.begin()));
        // add chain to collection
        nearChains.push_back(tempChain);
      }
    }

    return nearChains;
  }

  LocalizedRangeScanVector MapperGraph::FindNearLinkedScans(LocalizedRangeScan* pScan, kt_double maxDistance)
  {
    //利用当前帧的barycenter与linkscan的距离约束maxDistance来找linkscan
    NearScanVisitor* pVisitor = new NearScanVisitor(pScan, maxDistance, m_pMapper->m_pUseScanBarycenter->GetValue());
    //找到了合适的相邻帧
    LocalizedRangeScanVector nearLinkedScans = m_pTraversal->Traverse(GetVertex(pScan), pVisitor);
    delete pVisitor;

    return nearLinkedScans;
  }

  Pose2 MapperGraph::ComputeWeightedMean(const Pose2Vector& rMeans, const std::vector<Matrix3>& rCovariances) const
  {
    assert(rMeans.size() == rCovariances.size());

    // compute sum of inverses and create inverse list
    std::vector<Matrix3> inverses;
    inverses.reserve(rCovariances.size());

    Matrix3 sumOfInverses;
    const_forEach(std::vector<Matrix3>, &rCovariances)
    {
      Matrix3 inverse = iter->Inverse();
      inverses.push_back(inverse);

      sumOfInverses += inverse;
    }
    Matrix3 inverseOfSumOfInverses = sumOfInverses.Inverse();

    // compute weighted mean
    Pose2 accumulatedPose;
    kt_double thetaX = 0.0;
    kt_double thetaY = 0.0;

    Pose2Vector::const_iterator meansIter = rMeans.begin();
    const_forEach(std::vector<Matrix3>, &inverses)
    {
      Pose2 pose = *meansIter;
      kt_double angle = pose.GetHeading();
      thetaX += cos(angle);
      thetaY += sin(angle);

      Matrix3 weight = inverseOfSumOfInverses * (*iter);
      accumulatedPose += weight * pose;

      ++meansIter;
    }

    thetaX /= rMeans.size();
    thetaY /= rMeans.size();
    accumulatedPose.SetHeading(atan2(thetaY, thetaX));

    return accumulatedPose;
  }

  LocalizedRangeScanVector MapperGraph::FindPossibleLoopClosure(LocalizedRangeScan* pScan,
                                                                const Name& rSensorName,
                                                                kt_int32u& rStartNum)
  {
    LocalizedRangeScanVector chain;  // return value
    //得到 当前帧的扫描点的平均值
    Pose2 pose = pScan->GetReferencePose(m_pMapper->m_pUseScanBarycenter->GetValue());

    // possible loop closure chain should not include close scans that have a
    // path of links to the scan of interest
    const LocalizedRangeScanVector nearLinkedScans =
          FindNearLinkedScans(pScan, m_pMapper->m_pLoopSearchMaximumDistance->GetValue());  //从所有帧中找离当前帧最近的帧，exclude these

    kt_int32u nScans = static_cast<kt_int32u>(m_pMapper->m_pMapperSensorManager->GetScans(rSensorName).size());
    for (; rStartNum < nScans; rStartNum++)
    {
      //m_pMapperSensorManager中存储了所有的帧
      LocalizedRangeScan* pCandidateScan = m_pMapper->m_pMapperSensorManager->GetScan(rSensorName, rStartNum);

      Pose2 candidateScanPose = pCandidateScan->GetReferencePose(m_pMapper->m_pUseScanBarycenter->GetValue());
      //得到当前帧与所有帧(m_Scans)的第rStartNum帧之间的平方距离
      kt_double squaredDistance = candidateScanPose.GetPosition().SquaredDistance(pose.GetPosition());
      if (squaredDistance < math::Square(m_pMapper->m_pLoopSearchMaximumDistance->GetValue()) + KT_TOLERANCE)  //从头开始找，如果帧距离当前帧位置在m_pLoopSearchMaximumDistance（4m）之内，就先记录一下
      {
        // a linked scan cannot be in the chain
        if (find(nearLinkedScans.begin(), nearLinkedScans.end(), pCandidateScan) != nearLinkedScans.end())
        {
          chain.clear();
        }
        else
        {
          chain.push_back(pCandidateScan);
        }
      }
      else
      {
        // return chain if it is long "enough"
        if (chain.size() >= m_pMapper->m_pLoopMatchMinimumChainSize->GetValue())  //4m之内的不是nearLinkedScans的帧如果大于10帧，就作为可能的chain
        {
          return chain;
        }
        else
        {
          chain.clear();
        }
      }
    }

    return chain;
  }

  void MapperGraph::CorrectPoses()
  {
    // optimize scans!
    ScanSolver* pSolver = m_pMapper->m_pScanOptimizer;
    if (pSolver != NULL)
    {
      pSolver->Compute();

      const_forEach(ScanSolver::IdPoseVector, &pSolver->GetCorrections())
      {
        m_pMapper->m_pMapperSensorManager->GetScan(iter->first)->SetSensorPose(iter->second);
      }

      pSolver->Clear();
    }
  }

  ////////////////////////////////////////////////////////////////////////////////////////
  ////////////////////////////////////////////////////////////////////////////////////////
  ////////////////////////////////////////////////////////////////////////////////////////

  /**
   * Default constructor
   */
  Mapper::Mapper()
    : Module("Mapper")
    , m_Initialized(false)
    , m_pSequentialScanMatcher(NULL)
    , m_pMapperSensorManager(NULL)
    , m_pGraph(NULL)
    , m_pScanOptimizer(NULL)
  {
    InitializeParameters();
  }

  /**
   * Default constructor
   */
  Mapper::Mapper(const std::string& rName)
    : Module(rName)
    , m_Initialized(false)
    , m_pSequentialScanMatcher(NULL)
    , m_pMapperSensorManager(NULL)
    , m_pGraph(NULL)
    , m_pScanOptimizer(NULL)
  {
    InitializeParameters();
  }

  /**
   * Destructor
   */
  Mapper::~Mapper()
  {
    Reset();

    delete m_pMapperSensorManager;
  }

  void Mapper::InitializeParameters()
  {
    m_pUseScanMatching = new Parameter<kt_bool>(
        "UseScanMatching",
        "When set to true, the mapper will use a scan matching algorithm. "
        "In most real-world situations this should be set to true so that the "
        "mapper algorithm can correct for noise and errors in odometry and "
        "scan data. In some simulator environments where the simulated scan "
        "and odometry data are very accurate, the scan matching algorithm can "
        "produce worse results. In those cases set this to false to improve "
        "results.",
        true,
        GetParameterManager());

    m_pUseScanBarycenter = new Parameter<kt_bool>(
        "UseScanBarycenter",
        "Use the barycenter of scan endpoints to define distances between "
        "scans.",
        true, GetParameterManager());

    m_pMinimumTimeInterval = new Parameter<kt_double>(
        "MinimumTimeInterval",
        "Sets the minimum time between scans. If a new scan's time stamp is "
        "longer than MinimumTimeInterval from the previously processed scan, "
        "the mapper will use the data from the new scan. Otherwise, it will "
        "discard the new scan if it also does not meet the minimum travel "
        "distance and heading requirements. For performance reasons, it is "
        "generally it is a good idea to only process scans if a reasonable "
        "amount of time has passed. This parameter is particularly useful "
        "when there is a need to process scans while the robot is stationary.",
        3600, GetParameterManager());

    m_pMinimumTravelDistance = new Parameter<kt_double>(
        "MinimumTravelDistance",
        "Sets the minimum travel between scans.  If a new scan's position is "
        "more than minimumTravelDistance from the previous scan, the mapper "
        "will use the data from the new scan. Otherwise, it will discard the "
        "new scan if it also does not meet the minimum change in heading "
        "requirement. For performance reasons, generally it is a good idea to "
        "only process scans if the robot has moved a reasonable amount.",
        0.2, GetParameterManager());

    m_pMinimumTravelHeading = new Parameter<kt_double>(
        "MinimumTravelHeading",
        "Sets the minimum heading change between scans. If a new scan's "
        "heading is more than MinimumTravelHeading from the previous scan, the "
        "mapper will use the data from the new scan.  Otherwise, it will "
        "discard the new scan if it also does not meet the minimum travel "
        "distance requirement. For performance reasons, generally it is a good "
        "idea to only process scans if the robot has moved a reasonable "
        "amount.",
        math::DegreesToRadians(10), GetParameterManager());

    m_pScanBufferSize = new Parameter<kt_int32u>(
        "ScanBufferSize",
        "Scan buffer size is the length of the scan chain stored for scan "
        "matching. \"ScanBufferSize\" should be set to approximately "
        "\"ScanBufferMaximumScanDistance\" / \"MinimumTravelDistance\". The "
        "idea is to get an area approximately 20 meters long for scan "
        "matching. For example, if we add scans every MinimumTravelDistance == "
        "0.3 meters, then \"scanBufferSize\" should be 20 / 0.3 = 67.)",
        70, GetParameterManager());

    m_pScanBufferMaximumScanDistance = new Parameter<kt_double>(
        "ScanBufferMaximumScanDistance",
        "Scan buffer maximum scan distance is the maximum distance between the "
        "first and last scans in the scan chain stored for matching.",
        20.0, GetParameterManager());

    m_pLinkMatchMinimumResponseFine = new Parameter<kt_double>(
        "LinkMatchMinimumResponseFine",
        "Scans are linked only if the correlation response value is greater "
        "than this value.",
        0.8, GetParameterManager());

    m_pLinkScanMaximumDistance = new Parameter<kt_double>(
        "LinkScanMaximumDistance",
        "Maximum distance between linked scans.  Scans that are farther apart "
        "will not be linked regardless of the correlation response value.",
        10.0, GetParameterManager());

    m_pLoopSearchMaximumDistance = new Parameter<kt_double>(
        "LoopSearchMaximumDistance",
        "Scans less than this distance from the current position will be "
        "considered for a match in loop closure.",
        4.0, GetParameterManager());

    m_pDoLoopClosing = new Parameter<kt_bool>(
        "DoLoopClosing",
        "Enable/disable loop closure.",
        true, GetParameterManager());

    m_pLoopMatchMinimumChainSize = new Parameter<kt_int32u>(
        "LoopMatchMinimumChainSize",
        "When the loop closure detection finds a candidate it must be part of "
        "a large set of linked scans. If the chain of scans is less than this "
        "value we do not attempt to close the loop.",
        10, GetParameterManager());

    m_pLoopMatchMaximumVarianceCoarse = new Parameter<kt_double>(
        "LoopMatchMaximumVarianceCoarse",
        "The co-variance values for a possible loop closure have to be less "
        "than this value to consider a viable solution. This applies to the "
        "coarse search.",
        math::Square(0.4), GetParameterManager());

    m_pLoopMatchMinimumResponseCoarse = new Parameter<kt_double>(
        "LoopMatchMinimumResponseCoarse",
        "If response is larger then this, then initiate loop closure search at "
        "the coarse resolution.",
        0.8, GetParameterManager());

    m_pLoopMatchMinimumResponseFine = new Parameter<kt_double>(
        "LoopMatchMinimumResponseFine",
        "If response is larger then this, then initiate loop closure search at "
        "the fine resolution.",
        0.8, GetParameterManager());

    //////////////////////////////////////////////////////////////////////////////
    //    CorrelationParameters correlationParameters;

    m_pCorrelationSearchSpaceDimension = new Parameter<kt_double>(
        "CorrelationSearchSpaceDimension",
        "The size of the search grid used by the matcher. The search grid will "
        "have the size CorrelationSearchSpaceDimension * "
        "CorrelationSearchSpaceDimension",
        0.3, GetParameterManager());

    m_pCorrelationSearchSpaceResolution = new Parameter<kt_double>(
        "CorrelationSearchSpaceResolution",
        "The resolution (size of a grid cell) of the correlation grid.",
        0.01, GetParameterManager());

    m_pCorrelationSearchSpaceSmearDeviation = new Parameter<kt_double>(
        "CorrelationSearchSpaceSmearDeviation",
        "The point readings are smeared by this value in X and Y to create a "
        "smoother response.",
        0.03, GetParameterManager());


    //////////////////////////////////////////////////////////////////////////////
    //    CorrelationParameters loopCorrelationParameters;

    m_pLoopSearchSpaceDimension = new Parameter<kt_double>(
        "LoopSearchSpaceDimension",
        "The size of the search grid used by the matcher.",
        8.0, GetParameterManager());

    m_pLoopSearchSpaceResolution = new Parameter<kt_double>(
        "LoopSearchSpaceResolution",
        "The resolution (size of a grid cell) of the correlation grid.",
        0.05, GetParameterManager());

    m_pLoopSearchSpaceSmearDeviation = new Parameter<kt_double>(
        "LoopSearchSpaceSmearDeviation",
        "The point readings are smeared by this value in X and Y to create a "
        "smoother response.",
        0.03, GetParameterManager());

    //////////////////////////////////////////////////////////////////////////////
    // ScanMatcherParameters;

    m_pDistanceVariancePenalty = new Parameter<kt_double>(
        "DistanceVariancePenalty",
        "Variance of penalty for deviating from odometry when scan-matching. "
        "The penalty is a multiplier (less than 1.0) is a function of the "
        "delta of the scan position being tested and the odometric pose.",
        math::Square(0.3), GetParameterManager());

    m_pAngleVariancePenalty = new Parameter<kt_double>(
        "AngleVariancePenalty",
        "See DistanceVariancePenalty.",
        math::Square(math::DegreesToRadians(20)), GetParameterManager());

    m_pFineSearchAngleOffset = new Parameter<kt_double>(
        "FineSearchAngleOffset",
        "The range of angles to search during a fine search.",
        math::DegreesToRadians(0.2), GetParameterManager());

    m_pCoarseSearchAngleOffset = new Parameter<kt_double>(
        "CoarseSearchAngleOffset",
        "The range of angles to search during a coarse search.",
        math::DegreesToRadians(20), GetParameterManager());

    m_pCoarseAngleResolution = new Parameter<kt_double>(
        "CoarseAngleResolution",
        "Resolution of angles to search during a coarse search.",
        math::DegreesToRadians(2), GetParameterManager());

    m_pMinimumAnglePenalty = new Parameter<kt_double>(
        "MinimumAnglePenalty",
        "Minimum value of the angle penalty multiplier so scores do not become "
        "too small.",
        0.9, GetParameterManager());

    m_pMinimumDistancePenalty = new Parameter<kt_double>(
        "MinimumDistancePenalty",
        "Minimum value of the distance penalty multiplier so scores do not "
        "become too small.",
        0.5, GetParameterManager());

    m_pUseResponseExpansion = new Parameter<kt_bool>(
        "UseResponseExpansion",
        "Whether to increase the search space if no good matches are initially "
        "found.",
        false, GetParameterManager());
  }
  /* Adding in getters and setters here for easy parameter access */

  // General Parameters

  bool Mapper::getParamUseScanMatching()
  {
    return static_cast<bool>(m_pUseScanMatching->GetValue());
  }

  bool Mapper::getParamUseScanBarycenter()
  {
    return static_cast<bool>(m_pUseScanBarycenter->GetValue());
  }

  double Mapper::getParamMinimumTimeInterval()
  {
    return static_cast<double>(m_pMinimumTimeInterval->GetValue());
  }

  double Mapper::getParamMinimumTravelDistance()
  {
    return static_cast<double>(m_pMinimumTravelDistance->GetValue());
  }

  double Mapper::getParamMinimumTravelHeading()
  {
    return math::RadiansToDegrees(static_cast<double>(m_pMinimumTravelHeading->GetValue()));
  }

  int Mapper::getParamScanBufferSize()
  {
    return static_cast<int>(m_pScanBufferSize->GetValue());
  }

  double Mapper::getParamScanBufferMaximumScanDistance()
  {
    return static_cast<double>(m_pScanBufferMaximumScanDistance->GetValue());
  }

  double Mapper::getParamLinkMatchMinimumResponseFine()
  {
    return static_cast<double>(m_pLinkMatchMinimumResponseFine->GetValue());
  }

  double Mapper::getParamLinkScanMaximumDistance()
  {
    return static_cast<double>(m_pLinkScanMaximumDistance->GetValue());
  }

  double Mapper::getParamLoopSearchMaximumDistance()
  {
    return static_cast<double>(m_pLoopSearchMaximumDistance->GetValue());
  }

  bool Mapper::getParamDoLoopClosing()
  {
    return static_cast<bool>(m_pDoLoopClosing->GetValue());
  }

  int Mapper::getParamLoopMatchMinimumChainSize()
  {
    return static_cast<int>(m_pLoopMatchMinimumChainSize->GetValue());
  }

  double Mapper::getParamLoopMatchMaximumVarianceCoarse()
  {
    return static_cast<double>(std::sqrt(m_pLoopMatchMaximumVarianceCoarse->GetValue()));
  }

  double Mapper::getParamLoopMatchMinimumResponseCoarse()
  {
    return static_cast<double>(m_pLoopMatchMinimumResponseCoarse->GetValue());
  }

  double Mapper::getParamLoopMatchMinimumResponseFine()
  {
    return static_cast<double>(m_pLoopMatchMinimumResponseFine->GetValue());
  }

  // Correlation Parameters - Correlation Parameters

  double Mapper::getParamCorrelationSearchSpaceDimension()
  {
    return static_cast<double>(m_pCorrelationSearchSpaceDimension->GetValue());
  }

  double Mapper::getParamCorrelationSearchSpaceResolution()
  {
    return static_cast<double>(m_pCorrelationSearchSpaceResolution->GetValue());
  }

  double Mapper::getParamCorrelationSearchSpaceSmearDeviation()
  {
    return static_cast<double>(m_pCorrelationSearchSpaceSmearDeviation->GetValue());
  }

  // Correlation Parameters - Loop Correlation Parameters

  double Mapper::getParamLoopSearchSpaceDimension()
  {
    return static_cast<double>(m_pLoopSearchSpaceDimension->GetValue());
  }

  double Mapper::getParamLoopSearchSpaceResolution()
  {
    return static_cast<double>(m_pLoopSearchSpaceResolution->GetValue());
  }

  double Mapper::getParamLoopSearchSpaceSmearDeviation()
  {
    return static_cast<double>(m_pLoopSearchSpaceSmearDeviation->GetValue());
  }

  // ScanMatcher Parameters

  double Mapper::getParamDistanceVariancePenalty()
  {
    return std::sqrt(static_cast<double>(m_pDistanceVariancePenalty->GetValue()));
  }

  double Mapper::getParamAngleVariancePenalty()
  {
    return std::sqrt(static_cast<double>(m_pAngleVariancePenalty->GetValue()));
  }

  double Mapper::getParamFineSearchAngleOffset()
  {
    return static_cast<double>(m_pFineSearchAngleOffset->GetValue());
  }

  double Mapper::getParamCoarseSearchAngleOffset()
  {
    return static_cast<double>(m_pCoarseSearchAngleOffset->GetValue());
  }

  double Mapper::getParamCoarseAngleResolution()
  {
    return static_cast<double>(m_pCoarseAngleResolution->GetValue());
  }

  double Mapper::getParamMinimumAnglePenalty()
  {
    return static_cast<double>(m_pMinimumAnglePenalty->GetValue());
  }

  double Mapper::getParamMinimumDistancePenalty()
  {
    return static_cast<double>(m_pMinimumDistancePenalty->GetValue());
  }

  bool Mapper::getParamUseResponseExpansion()
  {
    return static_cast<bool>(m_pUseResponseExpansion->GetValue());
  }

  /* Setters for parameters */
  // General Parameters
  void Mapper::setParamUseScanMatching(bool b)
  {
    m_pUseScanMatching->SetValue((kt_bool)b);
  }

  void Mapper::setParamUseScanBarycenter(bool b)
  {
    m_pUseScanBarycenter->SetValue((kt_bool)b);
  }

  void Mapper::setParamMinimumTimeInterval(double d)
  {
    m_pMinimumTimeInterval->SetValue((kt_double)d);
  }

  void Mapper::setParamMinimumTravelDistance(double d)
  {
    m_pMinimumTravelDistance->SetValue((kt_double)d);
  }

  void Mapper::setParamMinimumTravelHeading(double d)
  {
    m_pMinimumTravelHeading->SetValue((kt_double)d);
  }

  void Mapper::setParamScanBufferSize(int i)
  {
    m_pScanBufferSize->SetValue((kt_int32u)i);
  }

  void Mapper::setParamScanBufferMaximumScanDistance(double d)
  {
    m_pScanBufferMaximumScanDistance->SetValue((kt_double)d);
  }

  void Mapper::setParamLinkMatchMinimumResponseFine(double d)
  {
    m_pLinkMatchMinimumResponseFine->SetValue((kt_double)d);
  }

  void Mapper::setParamLinkScanMaximumDistance(double d)
  {
    m_pLinkScanMaximumDistance->SetValue((kt_double)d);
  }

  void Mapper::setParamLoopSearchMaximumDistance(double d)
  {
    m_pLoopSearchMaximumDistance->SetValue((kt_double)d);
  }

  void Mapper::setParamDoLoopClosing(bool b)
  {
    m_pDoLoopClosing->SetValue((kt_bool)b);
  }

  void Mapper::setParamLoopMatchMinimumChainSize(int i)
  {
    m_pLoopMatchMinimumChainSize->SetValue((kt_int32u)i);
  }

  void Mapper::setParamLoopMatchMaximumVarianceCoarse(double d)
  {
    m_pLoopMatchMaximumVarianceCoarse->SetValue((kt_double)math::Square(d));
  }

  void Mapper::setParamLoopMatchMinimumResponseCoarse(double d)
  {
    m_pLoopMatchMinimumResponseCoarse->SetValue((kt_double)d);
  }

  void Mapper::setParamLoopMatchMinimumResponseFine(double d)
  {
    m_pLoopMatchMinimumResponseFine->SetValue((kt_double)d);
  }

  // Correlation Parameters - Correlation Parameters
  void Mapper::setParamCorrelationSearchSpaceDimension(double d)
  {
    m_pCorrelationSearchSpaceDimension->SetValue((kt_double)d);
  }

  void Mapper::setParamCorrelationSearchSpaceResolution(double d)
  {
    m_pCorrelationSearchSpaceResolution->SetValue((kt_double)d);
  }

  void Mapper::setParamCorrelationSearchSpaceSmearDeviation(double d)
  {
    m_pCorrelationSearchSpaceSmearDeviation->SetValue((kt_double)d);
  }


  // Correlation Parameters - Loop Closure Parameters
  void Mapper::setParamLoopSearchSpaceDimension(double d)
  {
    m_pLoopSearchSpaceDimension->SetValue((kt_double)d);
  }

  void Mapper::setParamLoopSearchSpaceResolution(double d)
  {
    m_pLoopSearchSpaceResolution->SetValue((kt_double)d);
  }

  void Mapper::setParamLoopSearchSpaceSmearDeviation(double d)
  {
    m_pLoopSearchSpaceSmearDeviation->SetValue((kt_double)d);
  }


  // Scan Matcher Parameters
  void Mapper::setParamDistanceVariancePenalty(double d)
  {
    m_pDistanceVariancePenalty->SetValue((kt_double)math::Square(d));
  }

  void Mapper::setParamAngleVariancePenalty(double d)
  {
    m_pAngleVariancePenalty->SetValue((kt_double)math::Square(d));
  }

  void Mapper::setParamFineSearchAngleOffset(double d)
  {
    m_pFineSearchAngleOffset->SetValue((kt_double)d);
  }

  void Mapper::setParamCoarseSearchAngleOffset(double d)
  {
    m_pCoarseSearchAngleOffset->SetValue((kt_double)d);
  }

  void Mapper::setParamCoarseAngleResolution(double d)
  {
    m_pCoarseAngleResolution->SetValue((kt_double)d);
  }

  void Mapper::setParamMinimumAnglePenalty(double d)
  {
    m_pMinimumAnglePenalty->SetValue((kt_double)d);
  }

  void Mapper::setParamMinimumDistancePenalty(double d)
  {
    m_pMinimumDistancePenalty->SetValue((kt_double)d);
  }

  void Mapper::setParamUseResponseExpansion(bool b)
  {
    m_pUseResponseExpansion->SetValue((kt_bool)b);
  }



  void Mapper::Initialize(kt_double rangeThreshold)
  {
    if (m_Initialized == false)
    {
      // create sequential scan and loop matcher
      m_pSequentialScanMatcher = ScanMatcher::Create(this,
                                                    m_pCorrelationSearchSpaceDimension->GetValue(),
                                                    m_pCorrelationSearchSpaceResolution->GetValue(),
                                                    m_pCorrelationSearchSpaceSmearDeviation->GetValue(),
                                                    rangeThreshold);
      assert(m_pSequentialScanMatcher);

      m_pMapperSensorManager = new MapperSensorManager(m_pScanBufferSize->GetValue(),
                                                       m_pScanBufferMaximumScanDistance->GetValue());

      m_pGraph = new MapperGraph(this, rangeThreshold);

      m_Initialized = true;
    }
  }

  void Mapper::Reset()
  {
    delete m_pSequentialScanMatcher;
    m_pSequentialScanMatcher = NULL;

    delete m_pGraph;
    m_pGraph = NULL;

    delete m_pMapperSensorManager;
    m_pMapperSensorManager = NULL;

    m_Initialized = false;
  }

  kt_bool Mapper::Process(Object*  /*pObject*/)
  {
    return true;
  }

  kt_bool Mapper::Process(LocalizedRangeScan* pScan)
  {
    if (pScan != NULL)
    {
      //通过 单例模式 实现了 pLaserRangeFinder的找回
      karto::LaserRangeFinder* pLaserRangeFinder = pScan->GetLaserRangeFinder();

      // validate scan
      if (pLaserRangeFinder == NULL || pScan == NULL || pLaserRangeFinder->Validate(pScan) == false)
      {
        return false;
      }

      if (m_Initialized == false)
      {
        // initialize mapper with range threshold from device
        Initialize(pLaserRangeFinder->GetRangeThreshold());
      }

      // get last scan
      LocalizedRangeScan* pLastScan = m_pMapperSensorManager->GetLastScan(pScan->GetSensorName());

      // update scans corrected pose based on last correction
      if (pLastScan != NULL)
      {
        // 如果有两个参数的话，Transform的内容是从pose1->pose2
        /*
        // pose transformation
        Pose2 m_Transform;

        Matrix3 m_Rotation;
        Matrix3 m_InverseRotation;
        */
        Transform lastTransform(pLastScan->GetOdometricPose(), pLastScan->GetCorrectedPose());
        pScan->SetCorrectedPose(lastTransform.TransformPose(pScan->GetOdometricPose()));
      }

      // test if scan is outside minimum boundary or if heading is larger then minimum heading
      if (!HasMovedEnough(pScan, pLastScan))
      {
        return false;
      }

      Matrix3 covariance;
      covariance.SetToIdentity();

      // correct scan (if not first scan)
      if (m_pUseScanMatching->GetValue() && pLastScan != NULL)
      {
        Pose2 bestPose;
        m_pSequentialScanMatcher->MatchScan(pScan,
                                            m_pMapperSensorManager->GetRunningScans(pScan->GetSensorName()),
                                                                                    bestPose,
                                                                                    covariance);
        pScan->SetSensorPose(bestPose);   //出来了最好的位置
      }

      // add scan to buffer and assign id
      m_pMapperSensorManager->AddScan(pScan);

      if (m_pUseScanMatching->GetValue())
      {
        // add to graph
        m_pGraph->AddVertex(pScan);
        m_pGraph->AddEdges(pScan, covariance);

        m_pMapperSensorManager->AddRunningScan(pScan);  // 这里面有RunningScan的维护

        //进行回环检测的步骤
        if (m_pDoLoopClosing->GetValue())
        {
          std::vector<Name> deviceNames = m_pMapperSensorManager->GetSensorNames();
          const_forEach(std::vector<Name>, &deviceNames)
          {
            m_pGraph->TryCloseLoop(pScan, *iter);
          }
        }
      }

      m_pMapperSensorManager->SetLastScan(pScan);   //每一次的 LastScan都是一个正常的pScan，而不是空的

      return true;
    }

    return false;
  }

  /**
   * Is the scan sufficiently far from the last scan?
   * @param pScan
   * @param pLastScan
   * @return true if the scans are sufficiently far
   */
  kt_bool Mapper::HasMovedEnough(LocalizedRangeScan* pScan, LocalizedRangeScan* pLastScan) const
  {
    // test if first scan
    if (pLastScan == NULL)
    {
      return true;
    }

    // test if enough time has passed
    kt_double timeInterval = pScan->GetTime() - pLastScan->GetTime();
    if (timeInterval >= m_pMinimumTimeInterval->GetValue())
    {
      return true;
    }

    Pose2 lastScannerPose = pLastScan->GetSensorAt(pLastScan->GetOdometricPose());
    Pose2 scannerPose = pScan->GetSensorAt(pScan->GetOdometricPose());

    // test if we have turned enough
    kt_double deltaHeading = math::NormalizeAngle(scannerPose.GetHeading() - lastScannerPose.GetHeading());
    if (fabs(deltaHeading) >= m_pMinimumTravelHeading->GetValue())
    {
      return true;
    }

    // test if we have moved enough
    kt_double squaredTravelDistance = lastScannerPose.GetPosition().SquaredDistance(scannerPose.GetPosition());
    if (squaredTravelDistance >= math::Square(m_pMinimumTravelDistance->GetValue()) - KT_TOLERANCE)
    {
      return true;
    }

    return false;
  }

  /**
   * Gets all the processed scans
   * @return all scans
   */
  const LocalizedRangeScanVector Mapper::GetAllProcessedScans() const
  {
    LocalizedRangeScanVector allScans;

    if (m_pMapperSensorManager != NULL)
    {
      allScans = m_pMapperSensorManager->GetAllScans();
    }

    return allScans;
  }

  /**
   * Adds a listener
   * @param pListener
   */
  void Mapper::AddListener(MapperListener* pListener)
  {
    m_Listeners.push_back(pListener);
  }

  /**
   * Removes a listener
   * @param pListener
   */
  void Mapper::RemoveListener(MapperListener* pListener)
  {
    std::vector<MapperListener*>::iterator iter = std::find(m_Listeners.begin(), m_Listeners.end(), pListener);
    if (iter != m_Listeners.end())
    {
      m_Listeners.erase(iter);
    }
  }

  void Mapper::FireInfo(const std::string& rInfo) const
  {
    const_forEach(std::vector<MapperListener*>, &m_Listeners)
    {
      (*iter)->Info(rInfo);
    }
  }

  void Mapper::FireDebug(const std::string& rInfo) const
  {
    const_forEach(std::vector<MapperListener*>, &m_Listeners)
    {
      MapperDebugListener* pListener = dynamic_cast<MapperDebugListener*>(*iter);

      if (pListener != NULL)
      {
        pListener->Debug(rInfo);
      }
    }
  }

  void Mapper::FireLoopClosureCheck(const std::string& rInfo) const
  {
    const_forEach(std::vector<MapperListener*>, &m_Listeners)
    {
      MapperLoopClosureListener* pListener = dynamic_cast<MapperLoopClosureListener*>(*iter);

      if (pListener != NULL)
      {
        pListener->LoopClosureCheck(rInfo);
      }
    }
  }

  void Mapper::FireBeginLoopClosure(const std::string& rInfo) const
  {
    const_forEach(std::vector<MapperListener*>, &m_Listeners)
    {
      MapperLoopClosureListener* pListener = dynamic_cast<MapperLoopClosureListener*>(*iter);

      if (pListener != NULL)
      {
        pListener->BeginLoopClosure(rInfo);
      }
    }
  }

  void Mapper::FireEndLoopClosure(const std::string& rInfo) const
  {
    const_forEach(std::vector<MapperListener*>, &m_Listeners)
    {
      MapperLoopClosureListener* pListener = dynamic_cast<MapperLoopClosureListener*>(*iter);

      if (pListener != NULL)
      {
        pListener->EndLoopClosure(rInfo);
      }
    }
  }

  void Mapper::SetScanSolver(ScanSolver* pScanOptimizer)
  {
    m_pScanOptimizer = pScanOptimizer;
  }

  MapperGraph* Mapper::GetGraph() const
  {
    return m_pGraph;
  }

  ScanMatcher* Mapper::GetSequentialScanMatcher() const
  {
    return m_pSequentialScanMatcher;
  }

  ScanMatcher* Mapper::GetLoopScanMatcher() const
  {
    return m_pGraph->GetLoopScanMatcher();
  }
}  // namespace karto
