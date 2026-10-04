// Display locale is independent of FACES' on-device input language. Native
// titles, paths, model IDs and message text remain the source's exact content.
const strings={
  ready:['Ready','준비됨'], nativeDefault:['Native default','네이티브 기본값'],
  noEntries:['No native entries','네이티브 항목 없음'],
  discovering:['Discovering native capabilities...','네이티브 기능 검색 중...'],
  noCapabilities:['No native capabilities reported','보고된 네이티브 기능 없음'],
  discoveryFailed:['Native discovery failed','네이티브 검색 실패'],
  deliveryUnknown:['Delivery unconfirmed','전달 미확인'],
  inspectStatus:['Inspect status; do not resend.','상태를 확인하세요. 다시 전송하지 마세요.'],
  command:['Command','명령'],slate:['Slate Dark','슬레이트 다크'],contrast:['High Contrast','고대비'],
  unavailable:['Middleware unavailable','미들웨어 사용 불가'],
  busy:['Gateway busy; check status','게이트웨이 처리 중. 상태를 확인하세요'],
  user:['USER','사용자'],assistant:['ASSISTANT','어시스턴트'],preview:['PREVIEW','미리보기'],
  message:['MESSAGE','메시지'],agent:['AGENT','어시스턴트'],system:['SYSTEM','시스템'],tool:['TOOL','도구'],
};
export const translate=(locale,key)=>strings[key]?.[locale==='ko'?1:0]??key;
export const validLocale=value=>value==='en'||value==='ko';
