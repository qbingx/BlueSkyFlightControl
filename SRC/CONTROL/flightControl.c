/**********************************************************************************************************
                                天穹飞控 —— 致力于打造中国最好的多旋翼开源飞控
                                Github: github.com/loveuav/BlueSkyFlightControl
                                技术讨论：bbs.loveuav.com/forum-68-1.html
 * @文件     flightControl.c
 * @说明     飞行控制，由于飞控硬件没完成，飞机也还没组装，暂时无法测试
 * @版本  	 V1.0
 * @作者     BlueSky
 * @网站     bbs.loveuav.com
 * @日期     2018.05 
**********************************************************************************************************/
#include "flightControl.h"
#include "flightStatus.h"
#include "board.h"
#include "ahrs.h"
#include "navigation.h"
#include "gps.h"
#include "motor.h"

FLIGHTCONTROL_t fc;

void FlightControlInit(void)
{
	//PID参数初始化
    //对于不同机型，PID参数需要进行调整
    //参数大小和电调型号有较大关系（电机电调的综合响应速度影响了PID参数）
	PID_SetParam(&fc.pid[ROLL_INNER],  1.5, 2.5, 0.1, 50, 30);
	PID_SetParam(&fc.pid[PITCH_INNER], 1.5, 2.5, 0.1, 50, 30);
	PID_SetParam(&fc.pid[YAW_INNER],   3.5, 5.0, 0, 50, 30);
	
	PID_SetParam(&fc.pid[ROLL_OUTER],  10.0, 0, 0, 0, 0);
	PID_SetParam(&fc.pid[PITCH_OUTER], 10.0, 0, 0, 0, 0);
	PID_SetParam(&fc.pid[YAW_OUTER],   5.0, 0, 0, 0, 0);	
	
	PID_SetParam(&fc.pid[VEL_X],	   2.0, 0.8, 0.0, 50, 30);	
	PID_SetParam(&fc.pid[VEL_Y],       2.0, 0.8, 0.0, 50, 30);	
	PID_SetParam(&fc.pid[VEL_Z],       2.0, 0.8, 0.01, 250, 30);	

	PID_SetParam(&fc.pid[POS_X],       1.5, 0, 0, 0, 0);
	PID_SetParam(&fc.pid[POS_Y],       1.5, 0, 0, 0, 0);
	PID_SetParam(&fc.pid[POS_Z],       2.5, 0, 0, 0, 0);		
}

/**********************************************************************************************************
*函 数 名: SetRcTarget
*功能说明: 设置摇杆控制量
*形    参: 摇杆控制量
*返 回 值: 无
**********************************************************************************************************/
void SetRcTarget(RCTARGET_t rcTarget)
{
    fc.rcTarget = rcTarget;
}

/**********************************************************************************************************
*函 数 名: AttitudeInnerControl
*功能说明: 姿态内环控制
*形    参: 角速度测量值 运行时间间隔
*返 回 值: 姿态内环控制量
**********************************************************************************************************/
static Vector3f_t AttitudeInnerControl(Vector3f_t gyro, float deltaT)
{
	static Vector3f_t rateControlOutput;
    
    //保留小数点后两位，减小数据误差对控制器的干扰（貌似没什么用）
    gyro.x = (float)((int32_t)(gyro.x * 100)) * 0.01f;
    gyro.y = (float)((int32_t)(gyro.y * 100)) * 0.01f;    
    gyro.z = (float)((int32_t)(gyro.z * 100)) * 0.01f;
    
    //计算角速度环控制误差：目标角速度 - 实际角速度（低通滤波后的陀螺仪测量值）
	fc.attInnerError.x = fc.attInnerTarget.x - gyro.x;	
	fc.attInnerError.y = fc.attInnerTarget.y - gyro.y;	
	fc.attInnerError.z = fc.attInnerTarget.z - gyro.z;		
		
    //PID算法，计算出角速度环的控制量
	rateControlOutput.x = PID_GetPID(&fc.pid[ROLL_INNER],  fc.attInnerError.x, deltaT);
	rateControlOutput.y = PID_GetPID(&fc.pid[PITCH_INNER], fc.attInnerError.y, deltaT);
	rateControlOutput.z = PID_GetPID(&fc.pid[YAW_INNER],   fc.attInnerError.z, deltaT);

	rateControlOutput.x = ConstrainInt32(rateControlOutput.x, -1200, +1200);	
	rateControlOutput.y = ConstrainInt32(rateControlOutput.y, -1200, +1200);		
    //限制偏航轴控制输出量
	rateControlOutput.z = -ConstrainInt32(rateControlOutput.z, -600, +600);	

    return rateControlOutput;
}

/**********************************************************************************************************
*函 数 名: SetAttInnerCtlTarget
*功能说明: 设置姿态内环控制目标量
*形    参: 控制目标值
*返 回 值: 无
**********************************************************************************************************/
void SetAttInnerCtlTarget(Vector3f_t target)
{
    fc.attInnerTarget = target;
}

/**********************************************************************************************************
*函 数 名: AltitudeInnerControl
*功能说明: 高度内环控制
*形    参: Z轴速度测量值 运行时间间隔
*返 回 值: 高度内环控制量
**********************************************************************************************************/
static float AltitudeInnerControl(float velZ, float deltaT)
{
	float velLpf;
    float altInnerControlOutput;
    //悬停油门中点
	int16_t throttleMid = 1000;
	
    //对速度测量值进行低通滤波，减少数据噪声对控制器的影响
    velLpf = velLpf * 0.992f + velZ * 0.008f;
    
    //计算控制误差
	fc.posInnerError.z = fc.posInnerTarget.z - velLpf;
    
    //PID算法，计算出高度内环（Z轴速度）的控制量
	altInnerControlOutput =  PID_GetP(&fc.pid[VEL_Z], fc.posInnerError.z);
	altInnerControlOutput += PID_GetI(&fc.pid[VEL_Z], fc.posInnerError.z, deltaT);
	altInnerControlOutput += ConstrainInt32(PID_GetD(&fc.pid[VEL_Z], fc.posInnerError.z, deltaT), -300, 300);
	
	altInnerControlOutput += throttleMid;

    return altInnerControlOutput;
}	

/**********************************************************************************************************
*函 数 名: SetAltInnerCtlTarget
*功能说明: 设置高度内环控制目标量
*形    参: 控制目标值
*返 回 值: 无
**********************************************************************************************************/
void SetAltInnerCtlTarget(float target)
{
    fc.posInnerTarget.z = target;
}

/**********************************************************************************************************
*函 数 名: FlightControlInnerLoop
*功能说明: 飞行内环控制，包括姿态内环和高度内环控制
*形    参: 角速度测量值
*返 回 值: 无
**********************************************************************************************************/
void FlightControlInnerLoop(Vector3f_t gyro)
{
    //计算函数运行时间间隔
	static uint32_t previousT;
	float deltaT = (GetSysTimeUs() - previousT) * 1e-6;	
	previousT = GetSysTimeUs();	    
    
    //姿态内环控制量
    Vector3f_t attInnerCtlValue;
    //高度内环控制量
    float      altInnerCtlValue;
    
    //姿态内环控制
    attInnerCtlValue = AttitudeInnerControl(gyro, deltaT);
    
    //高度内环控制
    //在手动模式下（MANUAL），油门直接由摇杆数据控制
    if(GetFlightMode() == MANUAL)
    {
        altInnerCtlValue = fc.rcTarget.throttle;
    }
    else
    {
        altInnerCtlValue = AltitudeInnerControl(GetCopterVelocity().z, deltaT);
    }
    
    //将内环控制量转换为动力电机输出
    MotorControl(attInnerCtlValue.x, attInnerCtlValue.y, attInnerCtlValue.z, altInnerCtlValue);
}

/**********************************************************************************************************
*函 数 名: AttitudeOuterControl
*功能说明: 姿态外环控制
*形    参: 无
*返 回 值: 无
**********************************************************************************************************/
void AttitudeOuterControl(void)
{
	uint8_t    flightMode;
	Vector3f_t angle;
	Vector3f_t attOuterCtlValue;
	float 	   yawRate = 0.5f;
	
	//获取当前飞机的姿态角
	angle = GetCopterAngle();
	//获取当前飞行模式
	flightMode = GetFlightMode();

    //保留小数点后两位，减小数据误差对控制器的干扰（貌似没什么用）	
    angle.x = (float)((int32_t)(angle.x * 100)) * 0.01f;
    angle.y = (float)((int32_t)(angle.y * 100)) * 0.01f;    
    angle.z = (float)((int32_t)(angle.z * 100)) * 0.01f;
    
	//计算姿态外环控制误差：目标角度 - 实际角度
    //手动和半自动模式以及GPS失效下，摇杆量直接作为横滚和俯仰的目标量
	if(flightMode == MANUAL || flightMode == SEMIAUTO || GpsGetFixStatus() == false)	
	{
        fc.attOuterError.x = fc.rcTarget.roll * 0.1f  - angle.x;
        fc.attOuterError.y = fc.rcTarget.pitch * 0.1f  - angle.y;
	}
	else
	{
        fc.attOuterError.x = fc.attOuterTarget.x - angle.x;
        fc.attOuterError.y = fc.attOuterTarget.y - angle.y;
	}	

    //PID算法，计算出姿态外环的控制量，并以一定比例缩放来控制PID参数的数值范围
	attOuterCtlValue.x = PID_GetP(&fc.pid[ROLL_OUTER],  fc.attOuterError.x) * 1.0f;
	attOuterCtlValue.y = PID_GetP(&fc.pid[PITCH_OUTER], fc.attOuterError.y) * 1.0f;

	//PID控制输出限幅，目的是限制飞行中最大的运动角速度，单位为°/s
    if(flightMode == MANUAL || flightMode == SEMIAUTO || GpsGetFixStatus() == false)	
	{
        attOuterCtlValue.x = ConstrainFloat(attOuterCtlValue.x, -300, 300);
        attOuterCtlValue.y = ConstrainFloat(attOuterCtlValue.y, -300, 300);
    }
    else
    {
        attOuterCtlValue.x = ConstrainFloat(attOuterCtlValue.x, -80, 80);
        attOuterCtlValue.y = ConstrainFloat(attOuterCtlValue.y, -80, 80);        
    }
    
	//若航向锁定被失能则直接将摇杆数值作为目标角速度
    if(fc.yawHoldFlag == ENABLE)
    {
        fc.attOuterError.z = fc.attOuterTarget.z - angle.z;
        if(fc.attOuterError.z <= -180)
            fc.attOuterError.z += 360;
        if (fc.attOuterError.z>= +180)
            fc.attOuterError.z -= 360;
        
        //计算偏航轴PID控制量
        attOuterCtlValue.z = -PID_GetP(&fc.pid[YAW_OUTER], fc.attOuterError.z) * 1.0f;	
        //限幅，单位为°/s
        attOuterCtlValue.z = ConstrainFloat(attOuterCtlValue.z, -100, 100);
	}
	else
	{
		attOuterCtlValue.z = fc.rcTarget.yaw * yawRate;
	}	
	
	//将姿态外环控制量作为姿态内环的控制目标
	SetAttInnerCtlTarget(attOuterCtlValue);
}

/**********************************************************************************************************
*函 数 名: SetAttOuterCtlTarget
*功能说明: 设置姿态外环控制目标量
*形    参: 控制目标值
*返 回 值: 无
**********************************************************************************************************/
void SetAttOuterCtlTarget(Vector3f_t target)
{
    fc.attOuterTarget.x = target.x;
    fc.attOuterTarget.y = target.y;
}

/**********************************************************************************************************
*函 数 名: SetYawCtlTarget
*功能说明: 设置偏航轴控制目标量
*形    参: 控制目标值
*返 回 值: 无
**********************************************************************************************************/
void SetYawCtlTarget(float target)
{
    fc.attOuterTarget.z = target;
}

/**********************************************************************************************************
*函 数 名: AltitudeOuterControl
*功能说明: 高度外环控制
*形    参: 无
*返 回 值: 无
**********************************************************************************************************/
void AltitudeOuterControl(void)
{
	float altLpf;
	float altOuterCtlValue;
    
    //若当前高度控制被禁用则退出函数
    if(fc.altCtlFlag == DISABLE)
        return;
    
	//获取当前飞机高度，并低通滤波，减少数据噪声对控制的干扰
	altLpf = altLpf * 0.99f + GetCopterPosition().z * 0.01f;
	
	//计算高度外环控制误差：目标高度 - 实际高度
	fc.posOuterError.z = fc.posOuterTarget.z - altLpf;

    //PID算法，计算出高度外环的控制量
	altOuterCtlValue = PID_GetP(&fc.pid[POS_Z], fc.posOuterError.z);
	
	//PID控制输出限幅
	altOuterCtlValue = ConstrainFloat(altOuterCtlValue, -200, 200);

	//将高度外环控制量作为高度内环的控制目标
	SetAltInnerCtlTarget(altOuterCtlValue);
}

/**********************************************************************************************************
*函 数 名: SetAltOuterCtlTarget
*功能说明: 设置高度外环控制目标量
*形    参: 控制目标值
*返 回 值: 无
**********************************************************************************************************/
void SetAltOuterCtlTarget(float target)
{
    fc.posOuterTarget.z = target;
}

/**********************************************************************************************************
*函 数 名: PositionInnerControl
*功能说明: 位置内环控制
*形    参: 无
*返 回 值: 无
**********************************************************************************************************/
void PositionInnerControl(void)
{
	Vector3f_t velLpf;
    Vector3f_t posInnerCtlOutput;

    //计算函数运行时间间隔
	static uint32_t previousT;
	float deltaT = (GetSysTimeUs() - previousT) * 1e-6;	
	previousT = GetSysTimeUs();	    
	
    //对速度测量值进行低通滤波，减少数据噪声对控制器的影响
    velLpf.x = velLpf.x * 0.99f + GetCopterVelocity().x * 0.01f;
    velLpf.y = velLpf.y * 0.99f + GetCopterVelocity().y * 0.01f;
    
    //计算控制误差
	fc.posInnerError.x = fc.posInnerTarget.x - velLpf.x;
	fc.posInnerError.y = fc.posInnerTarget.y - velLpf.y;
    
    //PID算法，计算出位置内环（X、Y轴速度）的控制量
	posInnerCtlOutput.x = PID_GetPID(&fc.pid[VEL_X], fc.posInnerError.x, deltaT);
	posInnerCtlOutput.y = PID_GetPID(&fc.pid[VEL_Y], fc.posInnerError.y, deltaT);

	//PID控制输出限幅
	posInnerCtlOutput.x = ConstrainFloat(posInnerCtlOutput.x, -200, 200);
	posInnerCtlOutput.y = ConstrainFloat(posInnerCtlOutput.y, -200, 200);
    
    //将位置内环控制量作为姿态外环的控制目标
	SetAttOuterCtlTarget(posInnerCtlOutput);
}	

/**********************************************************************************************************
*函 数 名: SetPosInnerCtlTarget
*功能说明: 设置位置内环控制目标量
*形    参: 控制目标值
*返 回 值: 无
**********************************************************************************************************/
void SetPosInnerCtlTarget(Vector3f_t target)
{
    fc.posInnerTarget.x = target.x;
    fc.posInnerTarget.y = target.y;
}

/**********************************************************************************************************
*函 数 名: PositionOuterControl
*功能说明: 位置外环控制
*形    参: 无
*返 回 值: 无
**********************************************************************************************************/
void PositionOuterControl(void)
{
	Vector3f_t posLpf;
    Vector3f_t posOuterCtlValue; 

    //若当前位置控制被禁用则退出函数
    if(fc.posCtlFlag == DISABLE)
        return;	
    
	//获取当前飞机位置，并低通滤波，减少数据噪声对控制的干扰
	posLpf.x = posLpf.x * 0.99f + GetCopterPosition().x * 0.01f;
	posLpf.y = posLpf.y * 0.99f + GetCopterPosition().y * 0.01f;
	
	//计算位置外环控制误差：目标位置 - 实际位置
	fc.posOuterError.x = fc.posOuterTarget.x - posLpf.x;
	fc.posOuterError.y = fc.posOuterTarget.y - posLpf.y;
    
    //PID算法，计算出位置外环的控制量
	posOuterCtlValue.x = PID_GetP(&fc.pid[POS_X], fc.posOuterError.x);
	posOuterCtlValue.y = PID_GetP(&fc.pid[POS_Y], fc.posOuterError.y);
	
    //将控制量转换到机体坐标系
    TransVelToBodyFrame(posOuterCtlValue, &posOuterCtlValue, GetCopterAngle().z);
    
	//PID控制输出限幅
	posOuterCtlValue.x = ConstrainFloat(posOuterCtlValue.x, -150, 150);
	posOuterCtlValue.y = ConstrainFloat(posOuterCtlValue.y, -150, 150);
    
	//将位置外环控制量作为位置内环的控制目标
	SetPosInnerCtlTarget(posOuterCtlValue);
}

/**********************************************************************************************************
*函 数 名: SetPosOuterCtlTarget
*功能说明: 设置位置外环控制目标量
*形    参: 控制目标值
*返 回 值: 无
**********************************************************************************************************/
void SetPosOuterCtlTarget(Vector3f_t target)
{
    fc.posOuterTarget.x = target.x;
    fc.posOuterTarget.y = target.y;
}

/**********************************************************************************************************
*函 数 名: SetAltCtlStatus
*功能说明: 设置高度控制状态
*形    参: 状态量（ENABLE或DISABLE）
*返 回 值: 无
**********************************************************************************************************/
void SetAltCtlStatus(uint8_t status)
{
    fc.altCtlFlag = status;
}

/**********************************************************************************************************
*函 数 名: SetPosCtlStatus
*功能说明: 设置高度控制状态
*形    参: 状态量（ENABLE或DISABLE）
*返 回 值: 无
**********************************************************************************************************/
void SetPosCtlStatus(uint8_t status)
{
    fc.posCtlFlag = status;
}

/**********************************************************************************************************
*函 数 名: SetYawHoldStatus
*功能说明: 设置航向锁定状态
*形    参: 状态量（ENABLE或DISABLE）
*返 回 值: 无
**********************************************************************************************************/
void SetYawHoldStatus(uint8_t status)
{
    fc.yawHoldFlag = status;
}

/**********************************************************************************************************
*函 数 名: GetAttInnerCtlError
*功能说明: 获取姿态内环控制误差
*形    参: 无
*返 回 值: 误差
**********************************************************************************************************/
Vector3f_t GetAttInnerCtlError(void)
{
    return fc.attInnerError;
}

/**********************************************************************************************************
*函 数 名: GetAttOuterCtlError
*功能说明: 获取姿态内环控制误差
*形    参: 无
*返 回 值: 误差
**********************************************************************************************************/
Vector3f_t GetAttOuterCtlError(void)
{
    return fc.attOuterError;
}

/**********************************************************************************************************
*函 数 名: GetPosInnerCtlError
*功能说明: 获取位置内环控制误差
*形    参: 无
*返 回 值: 误差
**********************************************************************************************************/
Vector3f_t GetPosInnerCtlError(void)
{
    return fc.posInnerError;
}

/**********************************************************************************************************
*函 数 名: GetPosOuterCtlError
*功能说明: 获取位置内环控制误差
*形    参: 无
*返 回 值: 误差
**********************************************************************************************************/
Vector3f_t GetPosOuterCtlError(void)
{
    return fc.posOuterError;
}

/**********************************************************************************************************
*函 数 名: FlightControlReset
*功能说明: 控制复位
*形    参: 无
*返 回 值: 无
**********************************************************************************************************/
void FlightControlReset(void)
{
    //PID积分项清零
    PID_ResetI(&fc.pid[ROLL_INNER]);
    PID_ResetI(&fc.pid[PITCH_INNER]);
    PID_ResetI(&fc.pid[YAW_INNER]);
    PID_ResetI(&fc.pid[VEL_X]);
    PID_ResetI(&fc.pid[VEL_Y]);
    PID_ResetI(&fc.pid[VEL_Z]);
    
    //高度控制目标复位为当前高度
    SetAltOuterCtlTarget(GetCopterPosition().z);
    //位置控制目标复位为当前位置
    SetPosOuterCtlTarget(GetCopterPosition());
}


