#pragma once

namespace ZOpt
{
	/** Dedicated server only: starts the periodic skeletal mesh sweep, which does nothing while the switches
	 *  ZOpt.SkelBuildables / ZOpt.SkelUnpossessed are 0 (they are set from the settings file by ZOConfig.cpp). */
	void StartSkelSweeper();
	void StopSkelSweeper();
}
