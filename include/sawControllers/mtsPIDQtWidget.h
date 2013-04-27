/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*-    */
/* ex: set filetype=cpp softtabstop=4 shiftwidth=4 tabstop=4 cindent expandtab: */

/*
  $Id$

  Author(s):  Zihan Chen
  Created on: 2013-02-20

  (C) Copyright 2013 Johns Hopkins University (JHU), All Rights Reserved.

--- begin cisst license - do not edit ---

This software is provided "as is" under an open source license, with
no warranty.  The complete license can be found in license.txt and
http://www.cisst.org/cisst/license.txt.

--- end cisst license ---
*/


#ifndef _mtsPIDQtWidget_h
#define _mtsPIDQtWidget_h

#include <cisstCommon/cmnXMLPath.h>
// #include <cisstOSAbstraction/osaTimeServer.h>
#include <cisstMultiTask/mtsComponent.h>
#include <cisstVector/vctQtWidgetDynamicVector.h>

#include <QtCore>
#include <QtGui>


class mtsPIDQtWidget: public QWidget, public mtsComponent
{
    Q_OBJECT;
    CMN_DECLARE_SERVICES(CMN_DYNAMIC_CREATION_ONEARG, CMN_LOG_ALLOW_DEFAULT);

public:
    mtsPIDQtWidget(const std::string & componentName, unsigned int numberOfAxis);
    mtsPIDQtWidget(const mtsComponentConstructorNameAndUInt &arg);
    ~mtsPIDQtWidget(){}

    void Configure(const std::string & filename = "");
    void Startup();
    void Cleanup();

protected:
    void Init(void);
    virtual void closeEvent(QCloseEvent * event);

private slots:
    //! qslot enable/disable mtsPID controller
    void slot_qcbEnablePID(bool toggle);
    //! qslot send desired pos when input changed
    void slot_PositionChanged(void);
    void slot_PGainChanged(void);
    void slot_DGainChanged(void);
    void slot_IGainChanged(void);
    //! qslot reset desired pos to current pos
    void slot_MaintainPosition(void);
    //! go to zero position
    void slot_ZeroPosition(void);
    //! qslot reset pid gain to current gain
    void slot_ResetPIDGain(void);

    void timerEvent(QTimerEvent * event);

private:
    //! setup PID controller GUI
    void setupUi(void);
    void EventErrorLimitHandler(void);

protected:

    struct ControllerPIDStruct {
        mtsFunctionVoid  ResetController;
        mtsFunctionWrite Enable;
        mtsFunctionWrite SetDesiredPositions;
        mtsFunctionRead  GetPositionJoint;

        mtsFunctionRead  GetPGain;
        mtsFunctionRead  GetDGain;
        mtsFunctionRead  GetIGain;

        mtsFunctionWrite SetPGain;
        mtsFunctionWrite SetDGain;
        mtsFunctionWrite SetIGain;
    } PID;

private:

    //! SetPosition
    vctDoubleVec desiredPos;

    int NumberOfAxis;
    vctDoubleVec analogIn;
    vctDoubleVec motorFeedbackCurrent;
    vctDoubleVec motorControlCurrent;
    vctBoolVec ampEnable;
    vctBoolVec ampStatus;
    bool powerStatus;
    unsigned short safetyRelay;

    // Interface
    vctDynamicVector<bool> lastEnableState;
    double startTime;

    // GUI: Commands
    QCheckBox* qcbEnablePID;
    vctQtWidgetDynamicVectorDoubleWrite * DesiredPositionWidget;
    vctQtWidgetDynamicVectorDoubleWrite * PGainWidget;
    vctQtWidgetDynamicVectorDoubleWrite * DGainWidget;
    vctQtWidgetDynamicVectorDoubleWrite * IGainWidget;
    vctQtWidgetDynamicVectorDoubleRead * CurrentPositionWidget;

    // Control
    QPushButton* quitButton;
};

CMN_DECLARE_SERVICES_INSTANTIATION(mtsPIDQtWidget);

#endif // _mtsPIDQtWidget_h