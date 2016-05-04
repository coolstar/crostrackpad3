#define DEFINEDESCRIPTOR 0
#include "internal.h"
#include "device.h"
#include <hiddevice.h>

//
// Globals
//

NTSTATUS
CyapaGetHidDescriptor(
IN WDFDEVICE Device,
IN WDFREQUEST Request
)
{
	NTSTATUS            status = STATUS_SUCCESS;
	size_t              bytesToCopy = 0;
	WDFMEMORY           memory;

	UNREFERENCED_PARAMETER(Device);

	CyapaPrint(DEBUG_LEVEL_VERBOSE, DBG_IOCTL,
		"CyapaGetHidDescriptor Entry\n");

	//
	// This IOCTL is METHOD_NEITHER so WdfRequestRetrieveOutputMemory
	// will correctly retrieve buffer from Irp->UserBuffer. 
	// Remember that HIDCLASS provides the buffer in the Irp->UserBuffer
	// field irrespective of the ioctl buffer type. However, framework is very
	// strict about type checking. You cannot get Irp->UserBuffer by using
	// WdfRequestRetrieveOutputMemory if the ioctl is not a METHOD_NEITHER
	// internal ioctl.
	//
	status = WdfRequestRetrieveOutputMemory(Request, &memory);

	if (!NT_SUCCESS(status))
	{
		CyapaPrint(DEBUG_LEVEL_ERROR, DBG_IOCTL,
			"WdfRequestRetrieveOutputMemory failed 0x%x\n", status);

		return status;
	}

	//
	// Use hardcoded "HID Descriptor" 
	//
	bytesToCopy = DefaultHidDescriptor.bLength;

	if (bytesToCopy == 0)
	{
		status = STATUS_INVALID_DEVICE_STATE;

		CyapaPrint(DEBUG_LEVEL_ERROR, DBG_IOCTL,
			"DefaultHidDescriptor is zero, 0x%x\n", status);

		return status;
	}

	status = WdfMemoryCopyFromBuffer(memory,
		0, // Offset
		(PVOID)&DefaultHidDescriptor,
		bytesToCopy);

	if (!NT_SUCCESS(status))
	{
		CyapaPrint(DEBUG_LEVEL_ERROR, DBG_IOCTL,
			"WdfMemoryCopyFromBuffer failed 0x%x\n", status);

		return status;
	}

	//
	// Report how many bytes were copied
	//
	WdfRequestSetInformation(Request, bytesToCopy);

	CyapaPrint(DEBUG_LEVEL_VERBOSE, DBG_IOCTL,
		"CyapaGetHidDescriptor Exit = 0x%x\n", status);

	return status;
}

NTSTATUS
CyapaGetReportDescriptor(
IN WDFDEVICE Device,
IN WDFREQUEST Request
)
{
	NTSTATUS            status = STATUS_SUCCESS;
	ULONG_PTR           bytesToCopy;
	WDFMEMORY           memory;

	UNREFERENCED_PARAMETER(Device);

	CyapaPrint(DEBUG_LEVEL_VERBOSE, DBG_IOCTL,
		"CyapaGetReportDescriptor Entry\n");

	//
	// This IOCTL is METHOD_NEITHER so WdfRequestRetrieveOutputMemory
	// will correctly retrieve buffer from Irp->UserBuffer. 
	// Remember that HIDCLASS provides the buffer in the Irp->UserBuffer
	// field irrespective of the ioctl buffer type. However, framework is very
	// strict about type checking. You cannot get Irp->UserBuffer by using
	// WdfRequestRetrieveOutputMemory if the ioctl is not a METHOD_NEITHER
	// internal ioctl.
	//
	status = WdfRequestRetrieveOutputMemory(Request, &memory);
	if (!NT_SUCCESS(status))
	{
		CyapaPrint(DEBUG_LEVEL_ERROR, DBG_IOCTL,
			"WdfRequestRetrieveOutputMemory failed 0x%x\n", status);

		return status;
	}

	//
	// Use hardcoded Report descriptor
	//
	bytesToCopy = DefaultHidDescriptor.DescriptorList[0].wReportLength;

	if (bytesToCopy == 0)
	{
		status = STATUS_INVALID_DEVICE_STATE;

		CyapaPrint(DEBUG_LEVEL_ERROR, DBG_IOCTL,
			"DefaultHidDescriptor's reportLength is zero, 0x%x\n", status);

		return status;
	}

	status = WdfMemoryCopyFromBuffer(memory,
		0,
		(PVOID)DefaultReportDescriptor,
		bytesToCopy);
	if (!NT_SUCCESS(status))
	{
		CyapaPrint(DEBUG_LEVEL_ERROR, DBG_IOCTL,
			"WdfMemoryCopyFromBuffer failed 0x%x\n", status);

		return status;
	}

	//
	// Report how many bytes were copied
	//
	WdfRequestSetInformation(Request, bytesToCopy);

	CyapaPrint(DEBUG_LEVEL_VERBOSE, DBG_IOCTL,
		"CyapaGetReportDescriptor Exit = 0x%x\n", status);

	return status;
}


NTSTATUS
CyapaGetDeviceAttributes(
IN WDFREQUEST Request
)
{
	NTSTATUS                 status = STATUS_SUCCESS;
	PHID_DEVICE_ATTRIBUTES   deviceAttributes = NULL;

	CyapaPrint(DEBUG_LEVEL_VERBOSE, DBG_IOCTL,
		"CyapaGetDeviceAttributes Entry\n");

	//
	// This IOCTL is METHOD_NEITHER so WdfRequestRetrieveOutputMemory
	// will correctly retrieve buffer from Irp->UserBuffer. 
	// Remember that HIDCLASS provides the buffer in the Irp->UserBuffer
	// field irrespective of the ioctl buffer type. However, framework is very
	// strict about type checking. You cannot get Irp->UserBuffer by using
	// WdfRequestRetrieveOutputMemory if the ioctl is not a METHOD_NEITHER
	// internal ioctl.
	//
	status = WdfRequestRetrieveOutputBuffer(Request,
		sizeof(HID_DEVICE_ATTRIBUTES),
		(PVOID *)&deviceAttributes,
		NULL);
	if (!NT_SUCCESS(status))
	{
		CyapaPrint(DEBUG_LEVEL_ERROR, DBG_IOCTL,
			"WdfRequestRetrieveOutputBuffer failed 0x%x\n", status);

		return status;
	}

	//
	// Set USB device descriptor
	//

	deviceAttributes->Size = sizeof(HID_DEVICE_ATTRIBUTES);
	deviceAttributes->VendorID = CYAPA_VID;
	deviceAttributes->ProductID = CYAPA_PID;
	deviceAttributes->VersionNumber = CYAPA_VERSION;

	//
	// Report how many bytes were copied
	//
	WdfRequestSetInformation(Request, sizeof(HID_DEVICE_ATTRIBUTES));

	CyapaPrint(DEBUG_LEVEL_VERBOSE, DBG_IOCTL,
		"CyapaGetDeviceAttributes Exit = 0x%x\n", status);

	return status;
}

NTSTATUS
CyapaGetString(
IN WDFREQUEST Request
)
{

	NTSTATUS status = STATUS_SUCCESS;
	PWSTR pwstrID;
	size_t lenID;
	WDF_REQUEST_PARAMETERS params;
	void *pStringBuffer = NULL;

	CyapaPrint(DEBUG_LEVEL_VERBOSE, DBG_IOCTL,
		"CyapaGetString Entry\n");

	WDF_REQUEST_PARAMETERS_INIT(&params);
	WdfRequestGetParameters(Request, &params);

	switch ((ULONG_PTR)params.Parameters.DeviceIoControl.Type3InputBuffer & 0xFFFF)
	{
	case HID_STRING_ID_IMANUFACTURER:
		pwstrID = L"CoolStar\0";
		break;

	case HID_STRING_ID_IPRODUCT:
		pwstrID = L"Cypress v3 Trackpad\0";
		break;

	case HID_STRING_ID_ISERIALNUMBER:
		pwstrID = L"123123123\0";
		break;

	default:
		pwstrID = NULL;
		break;
	}

	lenID = pwstrID ? wcslen(pwstrID)*sizeof(WCHAR) + sizeof(UNICODE_NULL) : 0;

	if (pwstrID == NULL)
	{

		CyapaPrint(DEBUG_LEVEL_ERROR, DBG_IOCTL,
			"CyapaGetString Invalid request type\n");

		status = STATUS_INVALID_PARAMETER;

		return status;
	}

	status = WdfRequestRetrieveOutputBuffer(Request,
		lenID,
		&pStringBuffer,
		&lenID);

	if (!NT_SUCCESS(status))
	{

		CyapaPrint(DEBUG_LEVEL_ERROR, DBG_IOCTL,
			"CyapaGetString WdfRequestRetrieveOutputBuffer failed Status 0x%x\n", status);

		return status;
	}

	RtlCopyMemory(pStringBuffer, pwstrID, lenID);

	WdfRequestSetInformation(Request, lenID);

	CyapaPrint(DEBUG_LEVEL_VERBOSE, DBG_IOCTL,
		"CyapaGetString Exit = 0x%x\n", status);

	return status;
}

NTSTATUS
CyapaProcessVendorReport(
IN PDEVICE_CONTEXT DevContext,
IN PVOID ReportBuffer,
IN ULONG ReportBufferLen,
OUT size_t* BytesWritten
)
{
	NTSTATUS status = STATUS_SUCCESS;
	WDFREQUEST reqRead;
	PVOID pReadReport = NULL;
	size_t bytesReturned = 0;

	CyapaPrint(DEBUG_LEVEL_VERBOSE, DBG_IOCTL,
		"CyapaProcessVendorReport Entry\n");

	status = WdfIoQueueRetrieveNextRequest(DevContext->ReportQueue,
		&reqRead);

	if (NT_SUCCESS(status))
	{
		status = WdfRequestRetrieveOutputBuffer(reqRead,
			ReportBufferLen,
			&pReadReport,
			&bytesReturned);

		if (NT_SUCCESS(status))
		{
			//
			// Copy ReportBuffer into read request
			//

			if (bytesReturned > ReportBufferLen)
			{
				bytesReturned = ReportBufferLen;
			}

			RtlCopyMemory(pReadReport,
				ReportBuffer,
				bytesReturned);

			//
			// Complete read with the number of bytes returned as info
			//

			WdfRequestCompleteWithInformation(reqRead,
				status,
				bytesReturned);

			CyapaPrint(DEBUG_LEVEL_INFO, DBG_IOCTL,
				"CyapaProcessVendorReport %d bytes returned\n", bytesReturned);

			//
			// Return the number of bytes written for the write request completion
			//

			*BytesWritten = bytesReturned;

			CyapaPrint(DEBUG_LEVEL_INFO, DBG_IOCTL,
				"%s completed, Queue:0x%p, Request:0x%p\n",
				DbgHidInternalIoctlString(IOCTL_HID_READ_REPORT),
				DevContext->ReportQueue,
				reqRead);
		}
		else
		{
			CyapaPrint(DEBUG_LEVEL_ERROR, DBG_IOCTL,
				"WdfRequestRetrieveOutputBuffer failed Status 0x%x\n", status);
		}
	}
	else
	{
		CyapaPrint(DEBUG_LEVEL_ERROR, DBG_IOCTL,
			"WdfIoQueueRetrieveNextRequest failed Status 0x%x\n", status);
	}

	CyapaPrint(DEBUG_LEVEL_VERBOSE, DBG_IOCTL,
		"CyapaProcessVendorReport Exit = 0x%x\n", status);

	return status;
}

NTSTATUS
CyapaReadReport(
IN PDEVICE_CONTEXT DevContext,
IN WDFREQUEST Request,
OUT BOOLEAN* CompleteRequest
)
{
	NTSTATUS status = STATUS_SUCCESS;

	CyapaPrint(DEBUG_LEVEL_VERBOSE, DBG_IOCTL,
		"CyapaReadReport Entry\n");

	//
	// Forward this read request to our manual queue
	// (in other words, we are going to defer this request
	// until we have a corresponding write request to
	// match it with)
	//

	status = WdfRequestForwardToIoQueue(Request, DevContext->ReportQueue);

	if (!NT_SUCCESS(status))
	{
		CyapaPrint(DEBUG_LEVEL_ERROR, DBG_IOCTL,
			"WdfRequestForwardToIoQueue failed Status 0x%x\n", status);
	}
	else
	{
		*CompleteRequest = FALSE;
	}

	CyapaPrint(DEBUG_LEVEL_VERBOSE, DBG_IOCTL,
		"CyapaReadReport Exit = 0x%x\n", status);

	return status;
}

NTSTATUS
CyapaWriteReport(
	IN PDEVICE_CONTEXT DevContext,
	IN WDFREQUEST Request
	)
{
	NTSTATUS status = STATUS_SUCCESS;
	WDF_REQUEST_PARAMETERS params;
	PHID_XFER_PACKET transferPacket = NULL;
	CyapaScrollControlReport* pReport = NULL;
	size_t bytesWritten = 0;

	CyapaPrint(DEBUG_LEVEL_VERBOSE, DBG_IOCTL,
		"CyapaWriteReport Entry\n");

	WDF_REQUEST_PARAMETERS_INIT(&params);
	WdfRequestGetParameters(Request, &params);

	if (params.Parameters.DeviceIoControl.InputBufferLength < sizeof(HID_XFER_PACKET))
	{
		CyapaPrint(DEBUG_LEVEL_ERROR, DBG_IOCTL,
			"CyapaWriteReport Xfer packet too small\n");

		status = STATUS_BUFFER_TOO_SMALL;
	}
	else
	{

		transferPacket = (PHID_XFER_PACKET)WdfRequestWdmGetIrp(Request)->UserBuffer;

		if (transferPacket == NULL)
		{
			CyapaPrint(DEBUG_LEVEL_ERROR, DBG_IOCTL,
				"CyapaWriteReport No xfer packet\n");

			status = STATUS_INVALID_DEVICE_REQUEST;
		}
		else
		{
			//
			// switch on the report id
			//

			switch (transferPacket->reportId)
			{
			case REPORTID_SCROLLCTRL:

				pReport = (CyapaScrollControlReport *)transferPacket->reportBuffer;

				if (pReport->Flag == 1) {
					DevContext->sc.scrollInertiaActive = 1;
				}
				else {
					DevContext->sc.scrollInertiaActive = 0;
				}

				break;

			default:

				CyapaPrint(DEBUG_LEVEL_ERROR, DBG_IOCTL,
					"CyapaWriteReport Unhandled report type %d\n", transferPacket->reportId);

				status = STATUS_INVALID_PARAMETER;

				break;
			}
		}
	}

	CyapaPrint(DEBUG_LEVEL_VERBOSE, DBG_IOCTL,
		"CyapaWriteReport Exit = 0x%x\n", status);

	return status;

}

NTSTATUS
CyapaGetFeature(
IN PDEVICE_CONTEXT DevContext,
IN WDFREQUEST Request,
OUT BOOLEAN* CompleteRequest
)
{
	UNREFERENCED_PARAMETER(CompleteRequest);
	NTSTATUS status = STATUS_SUCCESS;
	WDF_REQUEST_PARAMETERS params;
	PHID_XFER_PACKET transferPacket = NULL;

	CyapaPrint(DEBUG_LEVEL_VERBOSE, DBG_IOCTL,
		"CyapaGetFeature Entry\n");

	WDF_REQUEST_PARAMETERS_INIT(&params);
	WdfRequestGetParameters(Request, &params);

	if (params.Parameters.DeviceIoControl.OutputBufferLength < sizeof(HID_XFER_PACKET))
	{
		CyapaPrint(DEBUG_LEVEL_ERROR, DBG_IOCTL,
			"CyapaGetFeature Xfer packet too small\n");

		status = STATUS_BUFFER_TOO_SMALL;
	}
	else
	{

		transferPacket = (PHID_XFER_PACKET)WdfRequestWdmGetIrp(Request)->UserBuffer;

		if (transferPacket == NULL)
		{
			CyapaPrint(DEBUG_LEVEL_ERROR, DBG_IOCTL,
				"CyapaGetFeature No xfer packet\n");

			status = STATUS_INVALID_DEVICE_REQUEST;
		}
		else
		{
			//
			// switch on the report id
			//

			switch (transferPacket->reportId)
			{
			case REPORTID_FEATURE:
			{

				CyapaFeatureReport* pReport = NULL;

				if (transferPacket->reportBufferLen == sizeof(CyapaFeatureReport))
				{
					pReport = (CyapaFeatureReport*)transferPacket->reportBuffer;

					pReport->DeviceMode = DevContext->DeviceMode;

					pReport->DeviceIdentifier = 0;

					CyapaPrint(DEBUG_LEVEL_INFO, DBG_IOCTL,
						"CyapaGetFeature DeviceMode = 0x%x\n", DevContext->DeviceMode);
				}
				else
				{
					status = STATUS_INVALID_PARAMETER;

					CyapaPrint(DEBUG_LEVEL_ERROR, DBG_IOCTL,
						"CyapaGetFeature Error transferPacket->reportBufferLen (%d) is different from sizeof(CyapaFeatureReport) (%d)\n",
						transferPacket->reportBufferLen,
						sizeof(CyapaFeatureReport));
				}

				break;
			}

			default:

				CyapaPrint(DEBUG_LEVEL_ERROR, DBG_IOCTL,
					"CyapaGetFeature Unhandled report type %d\n", transferPacket->reportId);

				status = STATUS_INVALID_PARAMETER;

				break;
			}
		}
	}

	CyapaPrint(DEBUG_LEVEL_VERBOSE, DBG_IOCTL,
		"CyapaGetFeature Exit = 0x%x\n", status);

	return status;
}

PCHAR
DbgHidInternalIoctlString(
IN ULONG IoControlCode
)
{
	switch (IoControlCode)
	{
	case IOCTL_HID_GET_DEVICE_DESCRIPTOR:
		return "IOCTL_HID_GET_DEVICE_DESCRIPTOR";
	case IOCTL_HID_GET_REPORT_DESCRIPTOR:
		return "IOCTL_HID_GET_REPORT_DESCRIPTOR";
	case IOCTL_HID_READ_REPORT:
		return "IOCTL_HID_READ_REPORT";
	case IOCTL_HID_GET_DEVICE_ATTRIBUTES:
		return "IOCTL_HID_GET_DEVICE_ATTRIBUTES";
	case IOCTL_HID_WRITE_REPORT:
		return "IOCTL_HID_WRITE_REPORT";
	case IOCTL_HID_SET_FEATURE:
		return "IOCTL_HID_SET_FEATURE";
	case IOCTL_HID_GET_FEATURE:
		return "IOCTL_HID_GET_FEATURE";
	case IOCTL_HID_GET_STRING:
		return "IOCTL_HID_GET_STRING";
	case IOCTL_HID_ACTIVATE_DEVICE:
		return "IOCTL_HID_ACTIVATE_DEVICE";
	case IOCTL_HID_DEACTIVATE_DEVICE:
		return "IOCTL_HID_DEACTIVATE_DEVICE";
	case IOCTL_HID_SEND_IDLE_NOTIFICATION_REQUEST:
		return "IOCTL_HID_SEND_IDLE_NOTIFICATION_REQUEST";
	case IOCTL_HID_SET_OUTPUT_REPORT:
		return "IOCTL_HID_SET_OUTPUT_REPORT";
	case IOCTL_HID_GET_INPUT_REPORT:
		return "IOCTL_HID_GET_INPUT_REPORT";
	default:
		return "Unknown IOCTL";
	}
}
