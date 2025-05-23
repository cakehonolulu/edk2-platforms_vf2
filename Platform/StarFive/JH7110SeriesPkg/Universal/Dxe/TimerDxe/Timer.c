/** @file
  RISC-V Timer Architectural Protocol for U5 series platform.

  Copyright (c) 2019, Hewlett Packard Enterprise Development LP. All rights reserved.<BR>

  SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#include "Timer.h"
#include <Library/BaseRiscVSbiLib.h>
#include <U5Clint.h>
#include <sbi/riscv_asm.h>
#include <sbi/riscv_atomic.h>
#include <sbi/riscv_encoding.h>
#include <sbi/riscv_io.h>


BOOLEAN TimerHandlerReentry = FALSE;

//
// The handle onto which the Timer Architectural Protocol will be installed
//
STATIC EFI_HANDLE mTimerHandle = NULL;

//
// The Timer Architectural Protocol that this driver produces
//
EFI_TIMER_ARCH_PROTOCOL   mTimer = {
  TimerDriverRegisterHandler,
  TimerDriverSetTimerPeriod,
  TimerDriverGetTimerPeriod,
  TimerDriverGenerateSoftInterrupt
};

//
// Pointer to the CPU Architectural Protocol instance
//
EFI_CPU_ARCH_PROTOCOL *mCpu;

//
// The notification function to call on every timer interrupt.
// A bug in the compiler prevents us from initializing this here.
//
STATIC EFI_TIMER_NOTIFY mTimerNotifyFunction;

//
// The current period of the timer interrupt
//
STATIC UINT64 mTimerPeriod = 0;

/**
  U5 Series Timer Interrupt Handler.

  @param InterruptType    The type of interrupt that occured
  @param SystemContext    A pointer to the system context when the interrupt occured
**/

VOID
EFIAPI
TimerInterruptHandler (
  IN EFI_EXCEPTION_TYPE   InterruptType,
  IN EFI_SYSTEM_CONTEXT   SystemContext
  )
{
  EFI_TPL OriginalTPL;
  UINT64 RiscvTimer;

  if (TimerHandlerReentry) {
    //
    // MMode timer occurred when processing
    // SMode timer handler.
    //
    RiscvTimer = RiscVReadMachineTimerInterface();
    SbiSetTimer (RiscvTimer += mTimerPeriod);
    csr_clear(CSR_SIP, MIP_STIP);
    return;
  }
  TimerHandlerReentry = TRUE;

  OriginalTPL = gBS->RaiseTPL (TPL_HIGH_LEVEL);
  csr_clear(CSR_SIE, MIP_STIP); // Disable SMode timer int
  csr_clear(CSR_SIP, MIP_STIP);
  if (mTimerPeriod == 0) {
    gBS->RestoreTPL (OriginalTPL);
    csr_clear(CSR_SIE, MIP_STIP); // Disable SMode timer int
    return;
  }
  if (mTimerNotifyFunction != NULL) {
      mTimerNotifyFunction (mTimerPeriod);
  }
  RiscvTimer = RiscVReadMachineTimerInterface();
  SbiSetTimer (RiscvTimer += mTimerPeriod);
  gBS->RestoreTPL (OriginalTPL);
  csr_set(CSR_SIE, MIP_STIP); // enable SMode timer int
  TimerHandlerReentry = FALSE;
}

/**

  This function registers the handler NotifyFunction so it is called every time
  the timer interrupt fires.  It also passes the amount of time since the last
  handler call to the NotifyFunction.  If NotifyFunction is NULL, then the
  handler is unregistered.  If the handler is registered, then EFI_SUCCESS is
  returned.  If the CPU does not support registering a timer interrupt handler,
  then EFI_UNSUPPORTED is returned.  If an attempt is made to register a handler
  when a handler is already registered, then EFI_ALREADY_STARTED is returned.
  If an attempt is made to unregister a handler when a handler is not registered,
  then EFI_INVALID_PARAMETER is returned.  If an error occurs attempting to
  register the NotifyFunction with the timer interrupt, then EFI_DEVICE_ERROR
  is returned.

  @param This             The EFI_TIMER_ARCH_PROTOCOL instance.
  @param NotifyFunction   The function to call when a timer interrupt fires.  This
                          function executes at TPL_HIGH_LEVEL.  The DXE Core will
                          register a handler for the timer interrupt, so it can know
                          how much time has passed.  This information is used to
                          signal timer based events.  NULL will unregister the handler.

  @retval        EFI_SUCCESS            The timer handler was registered.
  @retval        EFI_UNSUPPORTED        The platform does not support timer interrupts.
  @retval        EFI_ALREADY_STARTED    NotifyFunction is not NULL, and a handler is already
                                        registered.
  @retval        EFI_INVALID_PARAMETER  NotifyFunction is NULL, and a handler was not
                                        previously registered.
  @retval        EFI_DEVICE_ERROR       The timer handler could not be registered.

**/
EFI_STATUS
EFIAPI
TimerDriverRegisterHandler (
  IN EFI_TIMER_ARCH_PROTOCOL  *This,
  IN EFI_TIMER_NOTIFY         NotifyFunction
  )
{
  DEBUG ((DEBUG_INFO, "TimerDriverRegisterHandler(0x%lx) called\n", NotifyFunction));
  mTimerNotifyFunction = NotifyFunction;
  return EFI_SUCCESS;
}

/**

  This function adjusts the period of timer interrupts to the value specified
  by TimerPeriod.  If the timer period is updated, then the selected timer
  period is stored in EFI_TIMER.TimerPeriod, and EFI_SUCCESS is returned.  If
  the timer hardware is not programmable, then EFI_UNSUPPORTED is returned.
  If an error occurs while attempting to update the timer period, then the
  timer hardware will be put back in its state prior to this call, and
  EFI_DEVICE_ERROR is returned.  If TimerPeriod is 0, then the timer interrupt
  is disabled.  This is not the same as disabling the CPU's interrupts.
  Instead, it must either turn off the timer hardware, or it must adjust the
  interrupt controller so that a CPU interrupt is not generated when the timer
  interrupt fires.


  @param This            The EFI_TIMER_ARCH_PROTOCOL instance.
  @param TimerPeriod     The rate to program the timer interrupt in 100 nS units.  If
                         the timer hardware is not programmable, then EFI_UNSUPPORTED is
                         returned.  If the timer is programmable, then the timer period
                         will be rounded up to the nearest timer period that is supported
                         by the timer hardware.  If TimerPeriod is set to 0, then the
                         timer interrupts will be disabled.

  @retval        EFI_SUCCESS       The timer period was changed.
  @retval        EFI_UNSUPPORTED   The platform cannot change the period of the timer interrupt.
  @retval        EFI_DEVICE_ERROR  The timer period could not be changed due to a device error.

**/
EFI_STATUS
EFIAPI
TimerDriverSetTimerPeriod (
  IN EFI_TIMER_ARCH_PROTOCOL  *This,
  IN UINT64                   TimerPeriod
  )
{
  UINT64 RiscvTimer;

  DEBUG ((DEBUG_INFO, "TimerDriverSetTimerPeriod(0x%lx)\n", TimerPeriod));

  if (TimerPeriod == 0) {
    mTimerPeriod = 0;
    csr_clear(CSR_SIE, MIP_STIP); // disable timer int
    return EFI_SUCCESS;
  }

  mTimerPeriod = TimerPeriod; // convert unit from 100ns to 1us
  RiscvTimer = RiscVReadMachineTimerInterface();
  SbiSetTimer(RiscvTimer + mTimerPeriod / 10);

  mCpu->EnableInterrupt(mCpu);
  csr_set(CSR_SIE, MIP_STIP); // enable timer int
  return EFI_SUCCESS;
}

/**

  This function retrieves the period of timer interrupts in 100 ns units,
  returns that value in TimerPeriod, and returns EFI_SUCCESS.  If TimerPeriod
  is NULL, then EFI_INVALID_PARAMETER is returned.  If a TimerPeriod of 0 is
  returned, then the timer is currently disabled.


  @param This            The EFI_TIMER_ARCH_PROTOCOL instance.
  @param TimerPeriod     A pointer to the timer period to retrieve in 100 ns units.  If
                         0 is returned, then the timer is currently disabled.

  @retval EFI_SUCCESS            The timer period was returned in TimerPeriod.
  @retval EFI_INVALID_PARAMETER  TimerPeriod is NULL.

**/
EFI_STATUS
EFIAPI
TimerDriverGetTimerPeriod (
  IN EFI_TIMER_ARCH_PROTOCOL   *This,
  OUT UINT64                   *TimerPeriod
  )
{
  *TimerPeriod = mTimerPeriod;
  return EFI_SUCCESS;
}

/**

  This function generates a soft timer interrupt. If the platform does not support soft
  timer interrupts, then EFI_UNSUPPORTED is returned. Otherwise, EFI_SUCCESS is returned.
  If a handler has been registered through the EFI_TIMER_ARCH_PROTOCOL.RegisterHandler()
  service, then a soft timer interrupt will be generated. If the timer interrupt is
  enabled when this service is called, then the registered handler will be invoked. The
  registered handler should not be able to distinguish a hardware-generated timer
  interrupt from a software-generated timer interrupt.


  @param This              The EFI_TIMER_ARCH_PROTOCOL instance.

  @retval EFI_SUCCESS       The soft timer interrupt was generated.
  @retval EFI_UNSUPPORTEDT  The platform does not support the generation of soft timer interrupts.

**/
EFI_STATUS
EFIAPI
TimerDriverGenerateSoftInterrupt (
  IN EFI_TIMER_ARCH_PROTOCOL  *This
  )
{
  return EFI_SUCCESS;
}

/**
  Initialize the Timer Architectural Protocol driver

  @param ImageHandle     ImageHandle of the loaded driver
  @param SystemTable     Pointer to the System Table

  @retval EFI_SUCCESS            Timer Architectural Protocol created
  @retval EFI_OUT_OF_RESOURCES   Not enough resources available to initialize driver.
  @retval EFI_DEVICE_ERROR       A device error occured attempting to initialize the driver.

**/
EFI_STATUS
EFIAPI
TimerDriverInitialize (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS  Status;

  //
  // Initialize the pointer to our notify function.
  //
  mTimerNotifyFunction = NULL;

  //
  // Make sure the Timer Architectural Protocol is not already installed in the system
  //
  ASSERT_PROTOCOL_ALREADY_INSTALLED (NULL, &gEfiTimerArchProtocolGuid);

  //
  // Find the CPU architectural protocol.
  //
  Status = gBS->LocateProtocol (&gEfiCpuArchProtocolGuid, NULL, (VOID **) &mCpu);
  ASSERT_EFI_ERROR (Status);

  //
  // Force the timer to be disabled
  //
  Status = TimerDriverSetTimerPeriod (&mTimer, 0);
  ASSERT_EFI_ERROR (Status);

  //
  // Install interrupt handler for RISC-V Timer.
  //
  Status = mCpu->RegisterInterruptHandler (mCpu, EXCEPT_RISCV_TIMER_INT, TimerInterruptHandler);
  ASSERT_EFI_ERROR (Status);

  //
  // Force the timer to be enabled at its default period
  //
  Status = TimerDriverSetTimerPeriod (&mTimer, DEFAULT_TIMER_TICK_DURATION);
  ASSERT_EFI_ERROR (Status);

  //
  // Install the Timer Architectural Protocol onto a new handle
  //
  Status = gBS->InstallMultipleProtocolInterfaces (
                  &mTimerHandle,
                  &gEfiTimerArchProtocolGuid, &mTimer,
                  NULL
                  );
  ASSERT_EFI_ERROR (Status);
  return Status;
}
