
ffmpeg_encode() {
	ffmpeg -i $1 -y -c:a aac        aac.aac    2>/dev/null
	ffmpeg -i $1 -y -c:a aac        aac.avi    2>/dev/null
	ffmpeg -i $1 -y -c:a aac        aac.mkv    2>/dev/null
	ffmpeg -i $1 -y -c:a aac        aac.mp4    2>/dev/null
	ffmpeg -i $1 -y -c:a aac        aac.ts     2>/dev/null
	ffmpeg -i $1 -y -c:a alac       alac.mkv   2>/dev/null
	ffmpeg -i $1 -y -c:a alac       alac.mp4   2>/dev/null
	ffmpeg -i $1 -y -c:a flac       flac.flac  2>/dev/null
	ffmpeg -i $1 -y -c:a flac       flac.ogg   2>/dev/null
	ffmpeg -i $1 -y -c:a libmp3lame -b:a 192k mp3_192.mp3 2>/dev/null
	ffmpeg -i $1 -y -c:a libmp3lame mp3.avi    2>/dev/null
	ffmpeg -i $1 -y -c:a libmp3lame mp3.mkv    2>/dev/null
	ffmpeg -i $1 -y -c:a libmp3lame mp3.mp3    2>/dev/null
	ffmpeg -i $1 -y -c:a libmp3lame mp3.ts     2>/dev/null
	ffmpeg -i $1 -y -c:a libopus    opus.mkv   2>/dev/null
	ffmpeg -i $1 -y -c:a libopus    opus.ogg   2>/dev/null
	ffmpeg -i $1 -y -c:a libvorbis  vorbis.mkv 2>/dev/null
	ffmpeg -i $1 -y -c:a libvorbis  vorbis.ogg 2>/dev/null
	ffmpeg -i $1 -y -c:a pcm_s16le  pcm.avi    2>/dev/null
	ffmpeg -i $1 -y -c:a pcm_s16le  pcm.caf    2>/dev/null
	ffmpeg -i $1 -y -c:a pcm_s16le  pcm.mkv    2>/dev/null
	ffmpeg -i $1 -y -c:a pcm_s16le  pcm.wav    2>/dev/null
	ffmpeg -i $1 -y -c:a wavpack    wv.wv      2>/dev/null
}

mkdir samples
cd samples
phiola rec -rat 48000 -m artist=A -m title=T -o 1.flac -u 0.001
ffmpeg_encode 1.flac
