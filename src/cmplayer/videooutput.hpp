#ifndef VIDEOOUTPUT_HPP
#define VIDEOOUTPUT_HPP

#include "stdafx.hpp"

struct MPContext;
struct vo_driver;
typedef quint32 uint32_t;
class VideoRenderer;
class PlayEngine;
class MPlayerOsdWrapper;


class VideoOutput {
public:
	VideoOutput(PlayEngine *engine);
	~VideoOutput();
	struct vo *vo_create(MPContext *mpctx);
	VideoRenderer *renderer() const;
	bool usingHwAccel() const;
private:
	static int preinit(struct vo */*vo*/, const char */*arg*/) {return 0;}
	static void uninit(struct vo */*vo*/) {}
	static int config(struct vo *vo, uint32_t w, uint32_t h, uint32_t , uint32_t , uint32_t , uint32_t fmt);
	static int control(struct vo *vo, uint32_t request, void *data);
	static void drawOsd(struct vo *vo, struct osd_state *osd);
	static void flipPage(struct vo *vo);
	static void checkEvents(struct vo *vo);
	static int drawSlice(struct vo *vo, uint8_t *src[], int stride[], int w, int h, int x, int y);
	static int queryFormat(int format);
	void drawImage(void *data);
	bool getImage(void *data);
	struct Data;
	Data *d;
};

#endif // VIDEOOUTPUT_HPP