#ifndef _YAACA_SERVER_
#define _YAACA_SERVER_ 1

/*

 {"cmd": "list"}
 [{ASI_CAMERA_INFO like struct}....]

 {"cmd" : "open", "idx": number}
 nothing

 {"cmd" : "close", "idx": number}
 nothing

 {"cmd" : "prop", "idx": number}
 {"controls" : [{ASI_CONTROL_CAPS lile struct}....],
  "Offset_HighestDR": int,
  "Offset_UnityGain": int,
  "Gain_LowestRN": int,
  "Offset_LowestRN": int
 }

 {"cmd" : "start", "idx": number}
 nothing

 {"cmd" : "stop", "idx": number}
 nothing

 {"cmd" : "stat", "idx": number}
 {"run_capture" :
  "vals" : [int values for controls ...],
  "auto" : [true/false auto for controls],
  "width" :
  "height" :
  "bin" :
  "type" :
  "start_x" :
  "start_y" :
  "cam_dropped" :
  "capture_error_status" :
  "capture_generation" :
  "run_save" :
  "recording" : 
  "dest" :
  "auto_debayer" :
  "save_error_status" :
  "save_generation" :
  "dropped" :
  "captured" :
  "ucaptured" :
 }

 {"cmd" : "set", "idx": number, others as above}
 nothing

 {"cmd" "data", "idx": number, data as above}
 raw image data in current format

  ret:
  -1 response too long
  -2 failed to parse
  -3 top level request not an object
  -4 cmd missing
  -5 unknown cmd
  -6 camera index out of range
  -7 idx missing
  -1000 + ASI_ERROR_CODE asi error code

 {"cmd" "pulse", "idx": number, "dir" : number (ASI_GUIDE_DIRECTION). "on": 0/1 }
 */

int yaaca_cmd(const char *req, char *resp, int len);

#endif
