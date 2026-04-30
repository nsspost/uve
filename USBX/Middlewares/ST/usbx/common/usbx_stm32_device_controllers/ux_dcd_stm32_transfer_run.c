/***************************************************************************
 * Copyright (c) 2024 Microsoft Corporation 
 * 
 * This program and the accompanying materials are made available under the
 * terms of the MIT License which is available at
 * https://opensource.org/licenses/MIT.
 * 
 * SPDX-License-Identifier: MIT
 **************************************************************************/


/**************************************************************************/
/**************************************************************************/
/**                                                                       */
/** USBX Component                                                        */
/**                                                                       */
/**   STM32 Controller Driver                                             */
/**                                                                       */
/**************************************************************************/
/**************************************************************************/

#define UX_SOURCE_CODE
#define UX_DCD_STM32_SOURCE_CODE


/* Include necessary system files.  */

#include "ux_api.h"
#include "ux_dcd_stm32.h"
#include "ux_utility.h"
#include "ux_device_stack.h"

volatile ULONG usbx_dcd_run_calls_dbg = 0UL;
volatile ULONG usbx_dcd_run_ep_dbg = 0UL;
volatile ULONG usbx_dcd_run_phase_dbg = 0UL;
volatile ULONG usbx_dcd_run_len_dbg = 0UL;
volatile ULONG usbx_dcd_run_ed_status_before_dbg = 0UL;
volatile ULONG usbx_dcd_run_ed_status_after_dbg = 0UL;
volatile ULONG usbx_dcd_run_wait_transfer_dbg = 0UL;
volatile ULONG usbx_dcd_run_done_seen_dbg = 0UL;
volatile ULONG usbx_dcd_run_tx_calls_dbg = 0UL;
volatile ULONG usbx_dcd_run_tx_status_dbg = 0UL;
volatile ULONG usbx_dcd_run_data_ptr_dbg = 0UL;
volatile ULONG usbx_dcd_run_first_word_dbg = 0UL;
volatile ULONG usbx_dcd_run_second_word_dbg = 0UL;
volatile ULONG usbx_dcd_pcd_epnum_dbg = 0UL;
volatile ULONG usbx_dcd_pcd_xfer_len_dbg = 0UL;
volatile ULONG usbx_dcd_pcd_xfer_count_dbg = 0UL;
volatile ULONG usbx_dcd_pcd_maxpacket_dbg = 0UL;
volatile ULONG usbx_dcd_pcd_type_dbg = 0UL;
volatile ULONG usbx_dcd_pcd_dieptsiz_dbg = 0UL;
volatile ULONG usbx_dcd_pcd_diepctl_dbg = 0UL;
volatile ULONG usbx_dcd_pcd_dtxfsts_dbg = 0UL;
volatile ULONG usbx_dcd_pcd_diepint_dbg = 0UL;


#if defined(UX_DEVICE_STANDALONE)
/**************************************************************************/
/*                                                                        */
/*  FUNCTION                                                RELEASE       */
/*                                                                        */
/*    _ux_dcd_stm32_transfer_request                       PORTABLE C     */
/*                                                            6.1.10      */
/*  AUTHOR                                                                */
/*                                                                        */
/*    Chaoqiong Xiao, Microsoft Corporation                               */
/*                                                                        */
/*  DESCRIPTION                                                           */
/*                                                                        */
/*    This function will initiate a transfer to a specific endpoint.      */
/*    If the endpoint is IN, the endpoint register will be set to accept  */
/*    the request.                                                        */
/*                                                                        */
/*    If the endpoint is IN, the endpoint FIFO will be filled with the    */
/*    buffer and the endpoint register set.                               */
/*                                                                        */
/*  INPUT                                                                 */
/*                                                                        */
/*    dcd_stm32                             Pointer to device controller  */
/*    transfer_request                      Pointer to transfer request   */
/*                                                                        */
/*  OUTPUT                                                                */
/*                                                                        */
/*    Completion Status                                                   */
/*                                                                        */
/*                                                                        */
/*  CALLS                                                                 */
/*                                                                        */
/*    HAL_PCD_EP_Transmit                   Transmit data                 */
/*    HAL_PCD_EP_Receive                    Receive data                  */
/*    _ux_utility_semaphore_get             Get semaphore                 */
/*                                                                        */
/*  CALLED BY                                                             */
/*                                                                        */
/*    STM32 Controller Driver                                             */
/*                                                                        */
/*  RELEASE HISTORY                                                       */
/*                                                                        */
/*    DATE              NAME                      DESCRIPTION             */
/*                                                                        */
/*  05-19-2020     Chaoqiong Xiao           Initial Version 6.0           */
/*  01-31-2022     Chaoqiong Xiao           Modified comment(s), used ST  */
/*                                            HAL library to drive the    */
/*                                            controller,                 */
/*                                            resulting in version 6.1.10 */
/*                                                                        */
/**************************************************************************/
UINT  _ux_dcd_stm32_transfer_run(UX_DCD_STM32 *dcd_stm32, UX_SLAVE_TRANSFER *transfer_request)
{
UX_INTERRUPT_SAVE_AREA

UX_SLAVE_ENDPOINT       *endpoint;
UX_DCD_STM32_ED         *ed;
ULONG                   ed_status;
HAL_StatusTypeDef       hal_status;


    /* Get the pointer to the logical endpoint from the transfer request.  */
    endpoint =  transfer_request -> ux_slave_transfer_request_endpoint;
    usbx_dcd_run_calls_dbg++;
    usbx_dcd_run_ep_dbg = endpoint -> ux_slave_endpoint_descriptor.bEndpointAddress;
    usbx_dcd_run_phase_dbg = transfer_request -> ux_slave_transfer_request_phase;
    usbx_dcd_run_len_dbg = transfer_request -> ux_slave_transfer_request_requested_length;

    /* Get the physical endpoint address in the endpoint container.  */
    ed =  (UX_DCD_STM32_ED *) endpoint -> ux_slave_endpoint_ed;

    UX_DISABLE

    /* Get current ED status.  */
    ed_status = ed -> ux_dcd_stm32_ed_status;
    usbx_dcd_run_ed_status_before_dbg = ed_status;

    /* Invalid state.  */
    if (_ux_system_slave -> ux_system_slave_device.ux_slave_device_state == UX_DEVICE_RESET)
    {
        transfer_request -> ux_slave_transfer_request_completion_code = UX_TRANSFER_BUS_RESET;
        UX_RESTORE
        return(UX_STATE_EXIT);
    }

    /* ED stalled.  */
    if (ed_status & UX_DCD_STM32_ED_STATUS_STALLED)
    {
        transfer_request -> ux_slave_transfer_request_completion_code = UX_TRANSFER_STALLED;
        UX_RESTORE
        return(UX_STATE_NEXT);
    }

    /* ED transfer in progress.  */
    if (ed_status & UX_DCD_STM32_ED_STATUS_TRANSFER)
    {
        if (ed_status & UX_DCD_STM32_ED_STATUS_DONE)
        {
            usbx_dcd_run_done_seen_dbg++;

            /* Keep used, stall and task pending bits.  */
            ed -> ux_dcd_stm32_ed_status &= (UX_DCD_STM32_ED_STATUS_USED |
                                        UX_DCD_STM32_ED_STATUS_STALLED |
                                        UX_DCD_STM32_ED_STATUS_TASK_PENDING);
            usbx_dcd_run_ed_status_after_dbg = ed -> ux_dcd_stm32_ed_status;
            UX_RESTORE
            return(UX_STATE_NEXT);
        }
        usbx_dcd_run_wait_transfer_dbg++;
        UX_RESTORE
        return(UX_STATE_WAIT);
    }


    /* Start transfer.  */
    ed -> ux_dcd_stm32_ed_status |= UX_DCD_STM32_ED_STATUS_TRANSFER;

    /* Check for transfer direction.  Is this a IN endpoint ? */
    if (transfer_request -> ux_slave_transfer_request_phase == UX_TRANSFER_PHASE_DATA_OUT)
    {
        uint32_t USBx_BASE = (uint32_t)dcd_stm32 -> pcd_handle -> Instance;
        uint8_t epnum = (uint8_t)(endpoint -> ux_slave_endpoint_descriptor.bEndpointAddress & 0x0FU);
        uint8_t *tx_data = transfer_request -> ux_slave_transfer_request_data_pointer;

        /* Transmit data.  */
        usbx_dcd_run_tx_calls_dbg++;
        usbx_dcd_run_data_ptr_dbg = (ULONG)tx_data;
        usbx_dcd_run_first_word_dbg = (tx_data != UX_NULL) ? __UNALIGNED_UINT32_READ(tx_data) : 0UL;
        usbx_dcd_run_second_word_dbg = (tx_data != UX_NULL) ? __UNALIGNED_UINT32_READ(tx_data + 4U) : 0UL;
        hal_status = HAL_PCD_EP_Transmit(dcd_stm32 -> pcd_handle,
                                         endpoint->ux_slave_endpoint_descriptor.bEndpointAddress,
                                         tx_data,
                                         transfer_request->ux_slave_transfer_request_requested_length);
        usbx_dcd_run_tx_status_dbg = (ULONG)hal_status;
        usbx_dcd_pcd_epnum_dbg = dcd_stm32 -> pcd_handle -> IN_ep[epnum].num;
        usbx_dcd_pcd_xfer_len_dbg = dcd_stm32 -> pcd_handle -> IN_ep[epnum].xfer_len;
        usbx_dcd_pcd_xfer_count_dbg = dcd_stm32 -> pcd_handle -> IN_ep[epnum].xfer_count;
        usbx_dcd_pcd_maxpacket_dbg = dcd_stm32 -> pcd_handle -> IN_ep[epnum].maxpacket;
        usbx_dcd_pcd_type_dbg = dcd_stm32 -> pcd_handle -> IN_ep[epnum].type;
        usbx_dcd_pcd_dieptsiz_dbg = USBx_INEP(epnum) -> DIEPTSIZ;
        usbx_dcd_pcd_diepctl_dbg = USBx_INEP(epnum) -> DIEPCTL;
        usbx_dcd_pcd_dtxfsts_dbg = USBx_INEP(epnum) -> DTXFSTS;
        usbx_dcd_pcd_diepint_dbg = USBx_INEP(epnum) -> DIEPINT;
    }
    else
    {

        /* We have a request for a SETUP or OUT Endpoint.  */
        /* Receive data.  */
        HAL_PCD_EP_Receive(dcd_stm32 -> pcd_handle,
                            endpoint->ux_slave_endpoint_descriptor.bEndpointAddress,
                            transfer_request->ux_slave_transfer_request_data_pointer,
                            transfer_request->ux_slave_transfer_request_requested_length);
    }

    /* Return to caller with WAIT.  */
    usbx_dcd_run_ed_status_after_dbg = ed -> ux_dcd_stm32_ed_status;
    UX_RESTORE
    return(UX_STATE_WAIT);
}
#endif /* defined(UX_DEVICE_STANDALONE) */
