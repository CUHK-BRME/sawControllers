/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*-    */
/* ex: set filetype=cpp softtabstop=4 shiftwidth=4 tabstop=4 cindent expandtab: */

/*

  Author(s):  Zihan Chen, Anton Deguet
  Created on: 2013-02-20

  (C) Copyright 2013 Johns Hopkins University (JHU), All Rights Reserved.

--- begin cisst license - do not edit ---

This software is provided "as is" under an open source license, with
no warranty.  The complete license can be found in license.txt and
http://www.cisst.org/cisst/license.txt.

--- end cisst license ---
*/


// system include
#include <iostream>

// cisst
#include <sawControllers/mtsTeleOperation.h>
#include <cisstMultiTask/mtsInterfaceProvided.h>
#include <cisstMultiTask/mtsInterfaceRequired.h>


CMN_IMPLEMENT_SERVICES_DERIVED_ONEARG(mtsTeleOperation, mtsTaskPeriodic, mtsTaskPeriodicConstructorArg);

mtsTeleOperation::mtsTeleOperation(const std::string & componentName, const double periodInSeconds):
    mtsTaskPeriodic(componentName, periodInSeconds)
{
    Init();
}

mtsTeleOperation::mtsTeleOperation(const mtsTaskPeriodicConstructorArg & arg):
    mtsTaskPeriodic(arg)
{
    Init();
}

void mtsTeleOperation::Init(void)
{
    Counter = 0;
    Scale = 0.2;

    // Initialize states
    this->IsClutched = false;
    this->IsOperatorPresent = false;
    this->IsEnabled = false;
    Slave.IsManipClutched = false;
    Slave.IsSUJClutched = false;

    this->StateTable.AddData(Master.PositionCartesianCurrent, "MasterCartesianPosition");
    this->StateTable.AddData(Slave.PositionCartesianCurrent, "SlaveCartesianPosition");

    // Setup CISST Interface
    mtsInterfaceRequired * masterRequired = AddInterfaceRequired("Master");
    if (masterRequired) {
        masterRequired->AddFunction("GetPositionCartesian", Master.GetPositionCartesian);
        masterRequired->AddFunction("SetPositionCartesian", Master.SetPositionCartesian);
        masterRequired->AddFunction("GetGripperPosition", Master.GetGripperPosition);
        masterRequired->AddFunction("SetRobotControlState", Master.SetRobotControlState);
    }

    mtsInterfaceRequired * slaveRequired = AddInterfaceRequired("Slave");
    if (slaveRequired) {
        slaveRequired->AddFunction("GetPositionCartesian", Slave.GetPositionCartesian);
        slaveRequired->AddFunction("SetPositionCartesian", Slave.SetPositionCartesian);
        slaveRequired->AddFunction("SetOpenAngle", Slave.SetOpenAngle);
        slaveRequired->AddFunction("SetRobotControlState", Slave.SetRobotControlState);

        slaveRequired->AddEventHandlerWrite(&mtsTeleOperation::EventHandlerManipClutch, this, "ManipClutchBtn");
        slaveRequired->AddEventHandlerWrite(&mtsTeleOperation::EventHandlerSUJClutch, this, "SUJClutchBtn");
    }

    // Footpedal events
    mtsInterfaceRequired * clutchRequired = AddInterfaceRequired("Clutch");
    if (clutchRequired) {
        clutchRequired->AddEventHandlerWrite(&mtsTeleOperation::EventHandlerClutched, this, "Button");
    }

    mtsInterfaceRequired * headRequired = AddInterfaceRequired("OperatorPresent");
    if (headRequired) {
        headRequired->AddEventHandlerWrite(&mtsTeleOperation::EventHandlerOperatorPresent, this, "Button");
    }

    mtsInterfaceProvided * providedSettings = AddInterfaceProvided("Setting");
    if (providedSettings) {
        providedSettings->AddCommandReadState(StateTable, StateTable.PeriodStats,
                                              "GetPeriodStatistics"); // mtsIntervalStatistics

        providedSettings->AddCommandWrite(&mtsTeleOperation::Enable, this, "Enable", mtsBool());
        providedSettings->AddCommandWrite(&mtsTeleOperation::SetScale, this, "SetScale", mtsDouble());
        providedSettings->AddCommandWrite(&mtsTeleOperation::SetRegistrationRotation, this,
                                          "SetRegistrationRotation", vctMatRot3());

        providedSettings->AddCommandVoid(&mtsTeleOperation::AllignMasterToSlave, this, "AllignMasterToSlave");
        providedSettings->AddCommandReadState(this->StateTable, Master.PositionCartesianCurrent, "GetPositionCartesianMaster");
        providedSettings->AddCommandReadState(this->StateTable, Slave.PositionCartesianCurrent, "GetPositionCartesianSlave");
    }
}

void mtsTeleOperation::Configure(const std::string & filename)
{
    CMN_LOG_CLASS_INIT_VERBOSE << "Configure: " << filename << std::endl;
}

void mtsTeleOperation::Startup(void)
{
    CMN_LOG_CLASS_INIT_VERBOSE << "mtsPIDQt::Startup" << std::endl;
}

void mtsTeleOperation::Run(void)
{
    ProcessQueuedCommands();
    ProcessQueuedEvents();

    // increment counter
    Counter++;

    // get master Cartesian position
    mtsExecutionResult executionResult;
    executionResult = Master.GetPositionCartesian(Master.PositionCartesianCurrent);
    if (!executionResult.IsOK()) {
        CMN_LOG_CLASS_RUN_ERROR << "Run: call to Master.GetPositionCartesian failed \""
                                << executionResult << "\"" << std::endl;
    }
    vctFrm4x4 masterPosition(Master.PositionCartesianCurrent.Position());

    // get slave Cartesian position
    executionResult = Slave.GetPositionCartesian(Slave.PositionCartesianCurrent);
    if (!executionResult.IsOK()) {
        CMN_LOG_CLASS_RUN_ERROR << "Run: call to Slave.GetPositionCartesian failed \""
                                << executionResult << "\"" << std::endl;
    }
    vctFrm4x4 slavePosition(Slave.PositionCartesianCurrent.Position());

    /*!
      mtsTeleOperation can run in 4 control modes, which is controlled by
      footpedal Clutch & OperatorPresent.

      Mode 1: OperatorPresent = False, Clutch = False
              MTM and PSM stop at their current position. If PSM ManipClutch is
              pressed, then the user can manually move PSM.
              NOTE: MTM always tries to allign its orientation with PSM's orientation

      Mode 2/3: OperatorPresent = False/True, Clutch = True
              MTM can move freely in workspace, however its orientation is locked
              PSM can not move

      Mode 4: OperatorPresent = True, Clutch = False
              PSM follows MTM motion
    */
    if (IsEnabled) {
        // follow mode
        if (!IsClutched && IsOperatorPresent) {
            // compute master Cartesian motion
            vctFrm4x4 masterCartesianMotion;
            masterCartesianMotion = Master.CartesianPrevious.Inverse() * masterPosition;

            // translation
            vct3 masterTranslation;
            vct3 slaveTranslation;
            masterTranslation = (masterPosition.Translation() - Master.CartesianPrevious.Translation());
            slaveTranslation = masterTranslation * this->Scale;
            slaveTranslation = RegistrationRotation * slaveTranslation + Slave.CartesianPrevious.Translation();

            // rotation
            vctMatRot3 slaveRotation;
            slaveRotation = RegistrationRotation * masterPosition.Rotation();

            // compute desired slave position
            vctFrm4x4 slaveCartesianDesired;
            slaveCartesianDesired.Translation().Assign(slaveTranslation);
            slaveCartesianDesired.Rotation().FromNormalized(slaveRotation);
            Slave.PositionCartesianDesired.Goal().FromNormalized(slaveCartesianDesired);

            // Slave go this cartesian position
            Slave.SetPositionCartesian(Slave.PositionCartesianDesired);

            // Gripper
            if (Master.GetGripperPosition.IsValid()) {
                double gripperPosition;
                Master.GetGripperPosition(gripperPosition);
                Slave.SetOpenAngle(gripperPosition);
            } else {
                Slave.SetOpenAngle(5.0 * cmnPI_180);
            }
        } else if (!IsClutched && !IsOperatorPresent) {
            // Do nothing
        }
    } else {
        CMN_LOG_CLASS_RUN_DEBUG << "mtsTeleOperation disabled" << std::endl;
    }
}

void mtsTeleOperation::Cleanup(void)
{
    CMN_LOG_CLASS_INIT_VERBOSE << "Cleanup" << std::endl;
}


void mtsTeleOperation::EventHandlerManipClutch(const prmEventButton & button)
{
    if (button.Type() == prmEventButton::PRESSED) {
        Slave.IsManipClutched = true;
        CMN_LOG_CLASS_RUN_ERROR << "EventHandlerManipClutch: ManipClutch pressed" << std::endl;
    } else {
        Slave.IsManipClutched = false;
        CMN_LOG_CLASS_RUN_ERROR << "EventHandlerManipClutch: ManipClutch released" << std::endl;
    }

    // Slave State
    if (IsEnabled && !IsOperatorPresent && Slave.IsManipClutched) {
        Slave.SetRobotControlState(mtsStdString("Manual"));
    } else if (IsEnabled) {
        Slave.SetRobotControlState(mtsStdString("Teleop"));
    }

    // Master
    if (IsEnabled && !Slave.IsManipClutched) {
        vctFrm4x4 masterCartesianDesired;
        masterCartesianDesired.Translation().Assign(MasterLockTranslation);
        vctMatRot3 masterRotation;
        masterRotation = RegistrationRotation.Inverse() * Slave.PositionCartesianCurrent.Position().Rotation();
        masterCartesianDesired.Rotation().FromNormalized(masterRotation);

        // Send Master command postion
        Master.PositionCartesianDesired.Goal().FromNormalized(masterCartesianDesired);
        Master.SetPositionCartesian(Master.PositionCartesianDesired);
    }
}

void mtsTeleOperation::EventHandlerSUJClutch(const prmEventButton & button)
{
    if (button.Type() == prmEventButton::PRESSED) {
        Slave.IsSUJClutched = true;
        CMN_LOG_CLASS_RUN_DEBUG << "EventHandlerSUJClutch: SUJClutch pressed" << std::endl;
    } else {
        Slave.IsSUJClutched = false;
        CMN_LOG_CLASS_RUN_DEBUG << "EventHandlerSUJClutch: SUJClutch released" << std::endl;
    }
}


void mtsTeleOperation::EventHandlerClutched(const prmEventButton & button)
{
    mtsExecutionResult executionResult;
    executionResult = Master.GetPositionCartesian(Master.PositionCartesianCurrent);
    if (!executionResult.IsOK()) {
        CMN_LOG_CLASS_RUN_ERROR << "EventHandlerClutched: call to Master.GetPositionCartesian failed \""
                                << executionResult << "\"" << std::endl;
    }
    executionResult = Slave.GetPositionCartesian(Slave.PositionCartesianCurrent);
    if (!executionResult.IsOK()) {
        CMN_LOG_CLASS_RUN_ERROR << "EventHandlerClutched: call to Slave.GetPositionCartesian failed \""
                                << executionResult << "\"" << std::endl;
    }

    if (button.Type() == prmEventButton::PRESSED) {
        this->IsClutched = true;
//        Master.SetMasterControlState(std::string("Clutch"));

        Master.PositionCartesianDesired.Goal().Rotation().FromNormalized(
                    Slave.PositionCartesianCurrent.Position().Rotation());
        Master.PositionCartesianDesired.Goal().Translation().Assign(
                    Master.PositionCartesianCurrent.Position().Translation());
//        Master.SetPositionCartesian(Master.PositionCartesianDesired);
    }
    else {
        this->IsClutched = false;
    }

    SetMasterControlState();
}

void mtsTeleOperation::EventHandlerOperatorPresent(const prmEventButton & button)
{
    if (button.Type() == prmEventButton::PRESSED) {
        this->IsOperatorPresent = true;
        CMN_LOG_CLASS_RUN_DEBUG << "EventHandlerOperatorPresent: OperatorPresent pressed" << std::endl;
    } else {
        this->IsOperatorPresent = false;
        CMN_LOG_CLASS_RUN_DEBUG << "EventHandlerOperatorPresent: OperatorPresent released" << std::endl;
    }

    SetMasterControlState();
}

void mtsTeleOperation::Enable(const mtsBool &enable)
{
    IsEnabled = enable.Data;

    // Set Master/Slave to Teleop (Cartesian Position Mode)
    SetMasterControlState();
    Slave.SetRobotControlState(mtsStdString("Teleop"));

    if (IsEnabled) {
        // Orientate Master with Slave
        vctFrm4x4 masterCartesianDesired;
        masterCartesianDesired.Translation().Assign(MasterLockTranslation);
        vctMatRot3 masterRotation;
        masterRotation = RegistrationRotation.Inverse() * Slave.PositionCartesianCurrent.Position().Rotation();
        masterCartesianDesired.Rotation().FromNormalized(masterRotation);

        // Send Master command postion
        Master.PositionCartesianDesired.Goal().FromNormalized(masterCartesianDesired);
        Master.SetPositionCartesian(Master.PositionCartesianDesired);
    }
}


void mtsTeleOperation::AllignMasterToSlave(void)
{
    //! \todo Reverse this procedure

//    vctFrm3 Rt_ms;
//    Rt_ms = ComputeMasterToSlaveFrame(masterPos.GetPosition());

//    // update Offset
//    Offset = Rt_ms * slavePos.Position().Inverse();

//    //! \todo add a flag
//    // set rot to identity()
//    Offset.Rotation().Assign(vctMatRot3::Identity());
}

void mtsTeleOperation::ComputeMasterToSlaveFrame(const vctFrm3 & mPos,
                                                 vctFrm3 & sPos)
{
    CMN_LOG_CLASS_RUN_DEBUG << "ComputeMasterToSlaveFrame: " << mPos << " " << sPos << std::endl;
}

void mtsTeleOperation::SetScale(const mtsDouble & scale)
{
    this->Scale = scale;
}

void mtsTeleOperation::SetRegistrationRotation(const vctMatRot3 & rotation)
{
    this->RegistrationRotation = rotation;
}


void mtsTeleOperation::SetMasterControlState(void)
{
    if (IsEnabled == false) {
        CMN_LOG_CLASS_RUN_WARNING << "TeleOperation is NOT enabled" << std::endl;
        return;
    }

    if (IsOperatorPresent && !IsClutched) {
        Master.SetRobotControlState(mtsStdString("Gravity"));
    } else if (IsOperatorPresent && IsClutched) {
        Master.SetRobotControlState(mtsStdString("Clutch"));
    } else if (!IsOperatorPresent && IsClutched) {
        Master.SetRobotControlState(mtsStdString("Clutch"));
    } else if (!IsOperatorPresent && !IsClutched) {
        MasterLockTranslation.Assign(Master.PositionCartesianCurrent.Position().Translation());
        Master.SetRobotControlState(mtsStdString("Teleop"));
    }

    // Update MTM/PSM previous position
    Master.CartesianPrevious.From(Master.PositionCartesianCurrent.Position());
    Slave.CartesianPrevious.From(Slave.PositionCartesianCurrent.Position());
}
