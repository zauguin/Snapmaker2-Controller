#include "../inc/MarlinConfig.h"

#if ENABLED(CANBUS_SUPPORT)

#include "../../HAL/HAL_GD32F1/HAL_can_STM32F1.h"
#include "../Marlin.h"
#include "temperature.h"
#include "configuration_store.h"
#include "ExecuterManager.h"
#include "Periphdevice.h"
#include "CanModule.h"
#include <EEPROM.h>
#include "../libs/GenerialFunctions.h"
#include "../SnapScreen/Screen.h"

CanModule CanModules;

#define CANID_BROCAST (1)

#define MODULE_MASK_BITS  0x1ff00000
#define MODULE_EXECUTER_PRINT 0
#define MODULE_EXECUTER_CNC 1
#define MODULE_EXECUTER_LASER 2
#define MODULE_LINEAR 3
#define MODULE_ROTATE 4
#define MODULE_ENCLOSER 5
#define MODULE_LIGHT 6
#define MODULE_AIRCONDITIONER 7


#define MAKE_ID(MID)  ((MID << 20) & MODULE_MASK_BITS)

#define FLASH_CAN_TABLE_ADDR  (0x8000000 + 32 * 1024)

/**
 *Init:Initialize module table
 */
void CanModule::Init(void) {
  SERIAL_ECHOLN("Module enum");
  CollectPlugModules();
  UpdateProcess();
  PrepareLinearModules();
  PrepareExecuterModules();
  PrepareExtendModules();
  for(int i=0;i<CanBusControlor.ModuleCount;i++) {
    SERIAL_ECHOLNPAIR("Basic ID:", Value32BitToString(CanBusControlor.ModuleMacList[i]));
  }

  for(int i=0;i<CanBusControlor.ExtendModuleCount;i++) {
    SERIAL_ECHOLNPAIR("Extend ID:", Value32BitToString(CanBusControlor.ExtendModuleMacList[i]));
  }
}

/**
 *CollectPlugModules:Collect the IDs of the pluged modules
 */
void CanModule::CollectPlugModules() {
  uint32_t tmptick;
  uint32_t ID;
  int i;
  uint32_t ExeIDs[3];

  //Collect module for can1
  while(1) {
    if(CanSendPacked(CANID_BROCAST, IDTYPE_EXTID, BASIC_CAN_NUM, FRAME_REMOTE, 0, 0) == true) {
      tmptick = millis() + 1000;
      while(tmptick > millis()) {
        ;
      }
      break;
    } else {
      SERIAL_ECHOLN("Send2 Error");
      break;
    }
  }

  LinearModuleCount = 0;
  for(i=0;i<CanBusControlor.ModuleCount;i++) {
    if((CanBusControlor.ModuleMacList[i] & MODULE_MASK_BITS) == MAKE_ID(MODULE_LINEAR))
      LinearModuleID[LinearModuleCount++] = CanBusControlor.ModuleMacList[i];
  }

  ExecuterCount = 0;
  ExeIDs[0] = MAKE_ID(MODULE_EXECUTER_PRINT);
  ExeIDs[1] = MAKE_ID(MODULE_EXECUTER_CNC);
  ExeIDs[2] = MAKE_ID(MODULE_EXECUTER_LASER);
  for(i=0;i<CanBusControlor.ModuleCount;i++) {
    ID = (CanBusControlor.ModuleMacList[i] & MODULE_MASK_BITS);
    if((ID == ExeIDs[0]) || (ID == ExeIDs[1]) || (ID == ExeIDs[2]))
      ExecuterID[ExecuterCount++] = CanBusControlor.ModuleMacList[i];
  }

  //Collect module for can2
  while(1) {
    if(CanSendPacked(CANID_BROCAST, IDTYPE_EXTID, EXTEND_CAN_NUM, FRAME_REMOTE, 0, 0) == true) {
      tmptick = millis() + 1000;
      while(tmptick > millis()) {
        ;
      }
      break;
    } else {
      SERIAL_ECHOLN("Send1 Error");
      break;
    }
  }

  //Update Endstops
  for(int i=0;i<20;i++)
    CanSendPacked(i, IDTYPE_STDID, BASIC_CAN_NUM, FRAME_DATA, 0, 0);
}

/**
 *PrepareLinearModules:Prepare for LinearModule
 */
void CanModule::PrepareLinearModules(void) {
  uint32_t tmptick;
  uint32_t i;
  uint32_t j;
  uint16_t tmpFuncID[9];
  uint8_t LinearAxisMark[9];
  uint8_t ModuleAxis;
  uint8_t CanNum = 2;
  bool prepared;
  uint8_t Buff[3] = {CMD_T_CONFIG, 0x00, 0x00};
  int Pins[3] = {X_DIR_PIN, Y_DIR_PIN, Z_DIR_PIN};
  
  WRITE(X_DIR_PIN, LOW);
  WRITE(Y_DIR_PIN, LOW);
  WRITE(Z_DIR_PIN, LOW);
  tmpEndstopBits = 0xffffffff;
  for(i=0;i<LinearModuleCount;i++) {
    prepared = false;
    for(j=0;j<3;j++) {
      Buff[1] = j;
      WRITE(Pins[j], HIGH);
      CanBusControlor.SendLongData(CanNum, LinearModuleID[i], Buff, 2);
      tmptick = millis() + 500;
      while(tmptick > millis()) {
        if(CanBusControlor.ProcessLongPacks(RecvBuff, 8) > 4) {
          if(RecvBuff[0] == CMD_R_CONFIG_REACK) {
            if(RecvBuff[1] == 1) {
              LinearAxisMark[i] = j;
              //Axis Endstop describe,etc 0-2:xyz 3:probe min 4-6:xyz max
              if(j == 0) {
                if(X_HOME_DIR == -1)  ModuleAxis = j;
                else ModuleAxis = j + 4;
              }
              else if(j == 1) {
                if(Y_HOME_DIR == -1)  ModuleAxis = j;
                else ModuleAxis = j + 4;
              }
              else if(j == 2) {
                if(Z_HOME_DIR == -1)  ModuleAxis = j;
                else ModuleAxis = j + 4;
              }
              
              LinearModuleLength[i] = (RecvBuff[2] << 8) | RecvBuff[3];
              prepared = true;
              //Limit position
              //RecvBuff[4];
              //Initialize Status
              if(RecvBuff[5] == 0) tmpEndstopBits &= ~(1 << ModuleAxis);
              SERIAL_ECHOLNPAIR("Length:", LinearModuleLength[i], " Axis:", LinearAxisMark[i]);
            }
          }
          break;
        }
      }
      WRITE(Pins[j], LOW);
      if(prepared == true)
        break;
    }
  }
  Endstop = tmpEndstopBits & (tmpEndstopBits >> 7) & (tmpEndstopBits >> 14);
  SERIAL_ECHOLNPAIR("Linear Module Endstop:", Value32BitToString(Endstop), " TmpBits:", Value32BitToString(tmpEndstopBits));
  
  //Get Linear module function ID
  for(i=0;i<LinearModuleCount;i++) {
    SendBuff[0] = CMD_T_REQUEST_FUNCID;
    SendBuff[1] = 0;
    CanBusControlor.SendLongData(CanNum, LinearModuleID[i], SendBuff, 2);
    tmptick = millis() + 100;
    tmpFuncID[i] = 0xffff;
    while(tmptick > millis()) {
      if(CanBusControlor.ProcessLongPacks(RecvBuff, 4) == 4) {
        if(RecvBuff[0] == CMD_R_REPORT_FUNCID) {
          tmpFuncID[i] = (RecvBuff[2] << 8) | RecvBuff[3];
          SERIAL_ECHOLNPAIR("MacIC:", LinearModuleID[i], " FuncID:", tmpFuncID[i]);
        }
        break;
      }
    }
  }

  SERIAL_ECHOLN("Bind LinearModule");

  //Bind Function ID with the message ID
  uint8_t AxisLinearCount[3] = {0, 0, 0};
  SendBuff[0] = CMD_T_CONFIG_FUNCID;
  SendBuff[1] = 0x01;
  for(i=0;i<LinearModuleCount;i++) {
    //Check if the max count of each axis reach 3 and if the FuncID is 0x0000
    if((LinearAxisMark[i] < 3) && (AxisLinearCount[LinearAxisMark[i]] < 3) && (tmpFuncID[i] == FUNC_REPORT_LIMIT)) {
      SendBuff[2] = 0;
      //Temporary for max endstops, example:first x axis is 0+4+0, the second x axis is 0+4+7 
      if(LinearAxisMark[i] == 0) {
        if(X_HOME_DIR > 0)
          SendBuff[3] = LinearAxisMark[i] + 4 + AxisLinearCount[LinearAxisMark[i]] * 7;
        else
          SendBuff[3] = LinearAxisMark[i] + AxisLinearCount[LinearAxisMark[i]] * 7;
      }
      if(LinearAxisMark[i] == 1) {
        if(Y_HOME_DIR > 0)
          SendBuff[3] = LinearAxisMark[i] + 4 + AxisLinearCount[LinearAxisMark[i]] * 7;
        else
          SendBuff[3] = LinearAxisMark[i] + AxisLinearCount[LinearAxisMark[i]] * 7;
      }
      if(LinearAxisMark[i] == 2) {
        if(Z_HOME_DIR > 0)
          SendBuff[3] = LinearAxisMark[i] + 4 + AxisLinearCount[LinearAxisMark[i]] * 7;
        else
          SendBuff[3] = LinearAxisMark[i] + AxisLinearCount[LinearAxisMark[i]] * 7;
      }
      SendBuff[4] = (uint8_t)(tmpFuncID[i] >> 8);
      SendBuff[5] = (uint8_t)(tmpFuncID[i]);
      MsgIDTable[SendBuff[3]] = tmpFuncID[i];
      //Axis count add 1
      AxisLinearCount[LinearAxisMark[i]]++;
      CanBusControlor.SendLongData(CanNum, LinearModuleID[i], SendBuff, 6);
      SERIAL_ECHOLNPAIR("Linear Module:", LinearModuleID[i], " MsgID:", SendBuff[3]);
    }
  }
  //Reserved 20 for high response
  MsgIDCount_CAN2 = 20;
  
}

/**
 *PrepareExecuterModules:Prepare for Executer module
 */
void CanModule::PrepareExecuterModules(void) {
  millis_t tmptick;
  uint32_t i;
  uint32_t j;
  int m;
  int k;
  int n;
  uint16_t LastFuncID;
  uint16_t FuncIDCount;
  uint8_t ExecuterMark[6];
  uint8_t Buff[3] = {CMD_T_CONFIG, 0x00, 0x00};
  int Pins[] = {E0_DIR_PIN};
 
  WRITE(E0_DIR_PIN, LOW);

  for(i=0;i<ExecuterCount;i++) {
    for(j=0;j<sizeof(Pins) / sizeof(Pins[0]);j++) {
      WRITE(Pins[j], HIGH);
      CanBusControlor.SendLongData(BASIC_CAN_NUM, ExecuterID[i], Buff, 3);
      
      tmptick = millis() + 100;
      //Unsed in current hardware design
      ExecuterMark[i] = 0xff;
      while(tmptick > millis())
      {
        if(CanBusControlor.ProcessLongPacks(RecvBuff, 20) == 2)
        {
          if(RecvBuff[0] == CMD_R_CONFIG_REACK) ExecuterMark[i] = RecvBuff[1];
          //SERIAL_ECHOLNPAIR("Executer Mark:", ExecuterMark[i]);
          break;
        }
      }
      WRITE(Pins[j], LOW);
      if(ExecuterMark[i] != 0xff) break;
    }
  }

  //Get Executer module function ID
  FuncIDCount = 0;
  for(i=0;i<ExecuterCount;i++) {
    SendBuff[0] = CMD_T_REQUEST_FUNCID;
    SendBuff[1] = 0;
    CanBusControlor.SendLongData(BASIC_CAN_NUM, ExecuterID[i], SendBuff, 2);
    tmptick = millis() + 2500;
    while(tmptick > millis()) {
      if(CanBusControlor.ProcessLongPacks(RecvBuff, 128) >= 4) {
        if(RecvBuff[0] == CMD_R_REPORT_FUNCID) {
          m = 2;
          for(k=0;k<RecvBuff[1];k++) {
            FuncIDList_CAN2[FuncIDCount] = (RecvBuff[m] << 8) | RecvBuff[m + 1];
            MacIDofFuncID_CAN2[FuncIDCount] = ExecuterID[i];
            SERIAL_ECHOLNPAIR("MacID:", MacIDofFuncID_CAN2[FuncIDCount], " FuncID:", FuncIDList_CAN2[FuncIDCount]);
            m = m + 2;
            FuncIDCount++;
          }
        }
        break;
      }
    }
  }

  tmptick = millis() + 200;
  while(tmptick > millis());

  //Get Priority from table
  for(i=0;i<FuncIDCount;i++) {
    FuncIDPriority_CAN2[i] = 15;
    for(j=0;j<sizeof(PriorityTable) / sizeof(PriorityTable[0]);j++)
    if(PriorityTable[j][0] == FuncIDList_CAN2[i]) {
      FuncIDPriority_CAN2[i] = PriorityTable[j][1]; 
      break;
    }
  }

  //Sort
  uint32_t tmpswapvalue;
  if(FuncIDCount > 1)
  {
    for(i=0;i<(FuncIDCount - 1);i++) {
      for(j=(i + 1);j<FuncIDCount;j++) {
        if(FuncIDPriority_CAN2[i] > FuncIDPriority_CAN2[j]) {
          tmpswapvalue = FuncIDPriority_CAN2[i];
          FuncIDPriority_CAN2[i] = FuncIDPriority_CAN2[j];
          FuncIDPriority_CAN2[j] = tmpswapvalue;
          tmpswapvalue = FuncIDList_CAN2[i];
          FuncIDList_CAN2[i] = FuncIDList_CAN2[j];
          FuncIDList_CAN2[j] = tmpswapvalue;
          tmpswapvalue = MacIDofFuncID_CAN2[i];
          MacIDofFuncID_CAN2[i] = MacIDofFuncID_CAN2[j];
          MacIDofFuncID_CAN2[j] = tmpswapvalue;
        }
      }
    }
  }

  //Mark the start MsgID
  m = MsgIDCount_CAN2;
  //Fill MsgID Table
  //Save the first Funcid to the MsgID list
  LastFuncID = ~FuncIDList_CAN2[0];
  for(i=0;i<FuncIDCount;i++) {
    if(LastFuncID != FuncIDList_CAN2[i])
      MsgIDTable[MsgIDCount_CAN2++] = FuncIDList_CAN2[i];
    LastFuncID = FuncIDList_CAN2[i];
  }
  
  //
  //Bind Function ID with the message ID
  SendBuff[0] = CMD_T_CONFIG_FUNCID;
  for(i=0;i<ExecuterCount;i++) {
    SendBuff[1] = 0x00;
    k = 2;
    for(j=0;j<FuncIDCount;j++) {
      if(ExecuterID[i] == MacIDofFuncID_CAN2[j]) {
        for(n=0;n<MsgIDCount_CAN2;n++) {
          if(MsgIDTable[n] == FuncIDList_CAN2[j]) {   
            SendBuff[k++] = (uint8_t)(n >> 8);
            SendBuff[k++] = (uint8_t)(n);
            SendBuff[k++] = (uint8_t)(FuncIDList_CAN2[j] >> 8);
            SendBuff[k++] = (uint8_t)(FuncIDList_CAN2[j]);
            SERIAL_ECHOLNPAIR("MsgID:", n, "-FuncID:", FuncIDList_CAN2[j]);
            SendBuff[1]++;
            m++;
            break;
          }
        }
      }
    }    
    CanBusControlor.SendLongData(BASIC_CAN_NUM, MacIDofFuncID_CAN2[i], SendBuff, k);
  }

  if((ExecuterID[0] & MODULE_MASK_BITS) == MAKE_ID(MODULE_EXECUTER_PRINT)) ExecuterHead.MachineType = MACHINE_TYPE_3DPRINT;
  else if(((ExecuterID[0] & MODULE_MASK_BITS) == MAKE_ID(MODULE_EXECUTER_CNC)) && (ExecuterMark[0] != 0xff)) ExecuterHead.MachineType = MACHINE_TYPE_CNC;
  else if(((ExecuterID[0] & MODULE_MASK_BITS) == MAKE_ID(MODULE_EXECUTER_LASER)) && (ExecuterMark[0] != 0xff)) ExecuterHead.MachineType = MACHINE_TYPE_LASER;
}

/**
 *PrepareExtendModules:Prepare for extend module
 */
void CanModule::PrepareExtendModules(void) {
  millis_t tmptick;
  uint32_t i;
  uint32_t j;
  int m;
  int k;
  int n;
  uint16_t LastFuncID;
  uint16_t FuncIDCount;

  SERIAL_ECHOLN("Prepare for extend module");
  
  //Get Executer module function ID
  FuncIDCount = 0;
  for(i=0;i<CanBusControlor.ExtendModuleCount;i++) {
    SendBuff[0] = CMD_T_REQUEST_FUNCID;
    SendBuff[1] = 0;
    CanBusControlor.SendLongData(EXTEND_CAN_NUM, CanBusControlor.ExtendModuleMacList[i], SendBuff, 2);
    tmptick = millis() + 2500;
    while(tmptick > millis()) {
      if(CanBusControlor.ProcessLongPacks(RecvBuff, 128) >= 4) {
        if(RecvBuff[0] == CMD_R_REPORT_FUNCID) {
          m = 2;
          for(k=0;k<RecvBuff[1];k++) {
            FuncIDList_CAN1[FuncIDCount] = (RecvBuff[m] << 8) | RecvBuff[m + 1];
            MacIDofFuncID_CAN1[FuncIDCount] = ExecuterID[i];
            SERIAL_ECHOLNPAIR("MacID:", MacIDofFuncID_CAN1[FuncIDCount], " FuncID:", FuncIDList_CAN1[FuncIDCount]);
            m = m + 2;
            FuncIDCount++;
          }
        }
        break;
      }
    }
  }

  tmptick = millis() + 200;
  while(tmptick > millis());

  //Get Priority from table
  for(i=0;i<FuncIDCount;i++) {
    FuncIDPriority_CAN1[i] = 15;
    for(j=0;j<sizeof(PriorityTable) / sizeof(PriorityTable[0]);j++)
    if(PriorityTable[j][0] == FuncIDList_CAN1[i]) {
      FuncIDPriority_CAN1[i] = PriorityTable[j][1]; 
      break;
    }
  }

  //Sort
  uint32_t tmpswapvalue;
  if(FuncIDCount > 1)
  {
    for(i=0;i<(FuncIDCount - 1);i++) {
      for(j=(i + 1);j<FuncIDCount;j++) {
        if(FuncIDPriority_CAN1[i] > FuncIDPriority_CAN1[j]) {
          tmpswapvalue = FuncIDPriority_CAN1[i];
          FuncIDPriority_CAN1[i] = FuncIDPriority_CAN1[j];
          FuncIDPriority_CAN1[j] = tmpswapvalue;
          tmpswapvalue = FuncIDList_CAN1[i];
          FuncIDList_CAN1[i] = FuncIDList_CAN1[j];
          FuncIDList_CAN1[j] = tmpswapvalue;
          tmpswapvalue = MacIDofFuncID_CAN1[i];
          MacIDofFuncID_CAN1[i] = MacIDofFuncID_CAN1[j];
          MacIDofFuncID_CAN1[j] = tmpswapvalue;
        }
      }
    }
  }

  //Mark the start MsgID
  m = MsgIDCount_CAN1;
  //Fill MsgID Table
  //Save the first Funcid to the MsgID list
  LastFuncID = ~FuncIDList_CAN1[0];
  for(i=0;i<FuncIDCount;i++) {
    if(LastFuncID != FuncIDList_CAN1[i])
      MsgIDTable[MsgIDCount_CAN1++] = FuncIDList_CAN1[i];
    LastFuncID = FuncIDList_CAN1[i];
  }
  
  //
  //Bind Function ID with the message ID
  SendBuff[0] = CMD_T_CONFIG_FUNCID;
  for(i=0;i<CanBusControlor.ExtendModuleCount;i++) {
    SendBuff[1] = 0x00;
    k = 2;
    for(j=0;j<FuncIDCount;j++) {
      if(CanBusControlor.ExtendModuleMacList[i] == MacIDofFuncID_CAN1[j]) {
        for(n=0;n<MsgIDCount_CAN1;n++) {
          if(MsgIDTable[n] == FuncIDList_CAN1[j]) {   
            SendBuff[k++] = (uint8_t)(n >> 8);
            SendBuff[k++] = (uint8_t)(n);
            SendBuff[k++] = (uint8_t)(FuncIDList_CAN1[j] >> 8);
            SendBuff[k++] = (uint8_t)(FuncIDList_CAN1[j]);
            SERIAL_ECHOLNPAIR("MsgID:", n, "-FuncID:", FuncIDList_CAN1[j]);
            SendBuff[1]++;
            m++;
            break;
          }
        }
      }
    }    
    CanBusControlor.SendLongData(EXTEND_CAN_NUM, MacIDofFuncID_CAN1[i], SendBuff, k);
  }
}

/**
 *LoadUpdateData:Load update data from flash
 *para Packindex:
 *para pData:The point to the buff 
 */
void CanModule::EraseUpdatePack(void) {
  uint32_t Address;
  FLASH_Unlock();
  Address = FLASH_UPDATE_CONTENT_INFO;
  FLASH_ErasePage(Address);
  Address = FLASH_UPDATE_CONTENT;
  FLASH_ErasePage(Address);
  FLASH_Lock();
  SERIAL_ECHOLN("Erase Flash Complete!");
}

/**
 *LoadUpdateData:Load update data from flash
 *para Packindex:
 *para pData:The point to the buff 
 */
bool CanModule::LoadUpdatePack(uint16_t Packindex, uint8_t *pData) {
  uint32_t Address;
  uint32_t Size;
  int i;
  uint16_t Packs;
  uint16_t PackSize = 128;
  Address = FLASH_UPDATE_CONTENT + 40;
  Size = *((uint32_t*)Address);
  Packs = Size / PackSize;
  if(Size % PackSize) Packs++;
  if(Packindex >= Packs) return false;
  Address = FLASH_UPDATE_CONTENT + 2048 + Packindex * PackSize;
  for(i=0;i<PackSize;i++) *pData++ = *((uint8_t*)Address++);
  return true;
}

/**
 *LoadUpdateInfo:Load update data from flash
 *para Version:Update file version
 *para StartID:
 *prar EndID:how many id type can be update when use the 
 *
 */
bool CanModule::LoadUpdateInfo(char *Version, uint16_t *StartID, uint16_t *EndID, uint32_t *Flag) {
  uint32_t Address;
  uint32_t Size;
  uint8_t Buff[33];
  Address = FLASH_UPDATE_CONTENT;
  for(int i=0;i<5;i++)
    Buff[i] = *((uint8_t*)Address++);
  if(Buff[0] != 1) return false;
  *StartID = (uint16_t)((Buff[1] << 8) | Buff[2]);
  *EndID = (uint16_t)((Buff[3] << 8) | Buff[4]);
  for(int i=0;i<32;i++)
    Version[i] = *((uint8_t*)Address++);
  Address = FLASH_UPDATE_CONTENT + 40;
  Size = *((uint32_t*)Address);
  SERIAL_ECHOLNPAIR("Size:", Size);
  Address = FLASH_UPDATE_CONTENT + 48;
  *Flag = *((uint32_t*)Address);
  SERIAL_ECHOLNPAIR("Flag:", *Flag);
  return true;
}

/**
 *Update:Send Start update process
 *para CanBum:Can port number
 *para ID:Module ID
 *para Version:Update file version
 *return :true if update success, or else false
 */
bool CanModule::UpdateModule(uint8_t CanNum, uint32_t ID, char *Version, uint32_t Flag) {
  uint32_t tmptick;
  uint16_t PackIndex;
  int i;
  int j;
  int err;

  SERIAL_ECHOLNPAIR("Module Start Update:" , ID);
  tmptick = millis() + 50;
  while(tmptick > millis());

  //Step1:send update version
  i = 0;
  err = 1;
  SendBuff[i++] = CMD_T_UPDATE_REQUEST;
  SendBuff[i++] = (Flag & 1)?1:0;
  for(j=0;(j<64) && (Version[j]!=0);j++) SendBuff[i++] = Version[j];
  CanBusControlor.SendLongData(CanNum, ID, SendBuff, i);
  tmptick = millis() + 6000;
  while(tmptick > millis()) {
    if(CanBusControlor.ProcessLongPacks(RecvBuff, 2) == 2) {
      if(RecvBuff[0] == CMD_R_UPDATE_REQUEST_REACK)
      {
        if(RecvBuff[1] == 0x00) {
          SERIAL_ECHOLN("Update Reject");
          break;
        }
        else if(RecvBuff[1] == 0x01) {
          SERIAL_ECHOLN("Requested");
          err = 0;
          break;
        }   
      }
    }
  }
  if(err) {
    SERIAL_ECHOLNPAIR("Module Update Fail:" , ID);
    return false;
  }

  //Step2:send update content
  tmptick = millis() + 2000;
  while(1) {
    if(CanBusControlor.ProcessLongPacks(RecvBuff, 4) == 4) {
      if(RecvBuff[0] == CMD_R_UPDATE_PACK_REQUEST) {
        PackIndex = (uint16_t)((RecvBuff[2] << 8) | RecvBuff[3]);
        if(LoadUpdatePack(PackIndex, &SendBuff[2]) == true) {
          SendBuff[0] = CMD_T_UPDATE_PACKDATA;
          SendBuff[1] = 0;
          CanBusControlor.SendLongData(CanNum, ID, SendBuff, 128 + 2);
        } else {
          SendBuff[0] = CMD_T_UPDATE_END;
          SendBuff[1] = 0;
          CanBusControlor.SendLongData(CanNum, ID, SendBuff, 2);
          break;
        }
        //delay = millis() + 500;
        //while(delay > millis());
        tmptick = millis() + 200;
      }
    }
    if(millis() > tmptick) {
      SERIAL_ECHOLNPAIR("Module Update Fail:" , ID);
      return false;
    }
  }
  tmptick = millis() + 20;
  while(tmptick > millis());
  SERIAL_ECHOLNPAIR("Module Update Complete:" , ID);

  return true;
}

/**
 *UpdateProcess:
 */
void CanModule::UpdateProcess(void)
{
  int i;
  millis_t tmptick;
  uint32_t UpdateFlag;
  uint16_t StartID, EndID, CurTypeID;
  uint8_t CanNum;

  CanNum = 0;
  char Version[64];
  //Load Update infomation and check if it is the module update file
  if(LoadUpdateInfo(Version, &StartID, &EndID, &UpdateFlag) == true) {
    SERIAL_ECHOLNPAIR("Version:", Version, "  StartID", StartID, "  EndID:", EndID);
    //Ergodic all modules which are suitable for updating
    for(i=0;i<CanBusControlor.ModuleCount;i++) {
      CurTypeID = CanBusControlor.ModuleMacList[i] & MODULE_MASK_BITS;
      if((CurTypeID >= StartID) && (CurTypeID <= EndID)) {
        UpdateModule(CanNum, CanBusControlor.ModuleMacList[i], Version, UpdateFlag);
      } else {
        
      }
    }
    EraseUpdatePack();
    HMI.SendUpdateComplete(1);
    tmptick = millis() + 2000;
    while(tmptick > millis());
  }
}

/**
 *UpdateEndstops:Update endstop from can
 */
int CanModule::UpdateEndstops(uint8_t *pBuff) {
  uint16_t MsgID;
  uint16_t index;
  
  MsgID = (uint16_t)((pBuff[0] << 8) | pBuff[1]);
  index = MsgID;
  tmpEndstopBits |= (1 << index);
  if(pBuff[2] == 0) tmpEndstopBits &= ~(1 << index);
  Endstop = tmpEndstopBits & (tmpEndstopBits >> 7) & (tmpEndstopBits >> 14);
  SERIAL_ECHOLN("EndStop");
  return 0;
}

/**
 * SetFunctionValue:Post Value to the specific Function ID Modules
 * Para CanNum:The Can port number
 * Para FuncID:
 * Para pBuff:The Pointer of the data to be sent
 * Para Len:The length of the data,nomore than 8
 */
int CanModule::SetFunctionValue(uint8_t CanNum, uint16_t FuncID, uint8_t *pBuff, uint8_t Len) {
  int i;
  for(i=0;i<MsgIDCount_CAN2;i++) {
    if(MsgIDTable[i] == FuncID) {
      CanSendPacked(i, IDTYPE_STDID, CanNum, FRAME_DATA, Len, pBuff);
      break;
    }
  }
  return 0;
}


#endif // ENABLED CANBUS_SUPPORT