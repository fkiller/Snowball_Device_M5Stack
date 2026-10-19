// Read-only, revision-bound chunks preserve complete native text, including
// a single turn larger than the middleware's control-response limit.
export async function readTranscript(api,sessionKey){
  const chunks=[];let offset=0,revision;
  for(;;){
    const page=await api('/v1/transcript',{sessionKey,offset,...(revision?{revision}:{})});
    if(page.sessionKey!==sessionKey||page.offset!==offset||!/^[a-f0-9]{64}$/.test(page.revision)||revision&&page.revision!==revision||typeof page.chunk!=='string'||page.chunk.length>256*1024||page.nextOffset!==null&&(page.chunk.length===0||page.nextOffset!==offset+page.chunk.length))throw Error('invalid_transcript_page');
    chunks.push(page.chunk);revision=page.revision;
    if(page.nextOffset===null)break;
    offset=page.nextOffset;
  }
  const turns=JSON.parse(chunks.join(''));if(!Array.isArray(turns))throw Error('invalid_transcript_page');
  return turns;
}
