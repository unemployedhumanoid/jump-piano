#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/PlayerObject.hpp>
#include <algorithm>
#include <cmath>
#include <vector>

using namespace geode::prelude;

namespace {
	constexpr int NOTE_COUNT = 7;
	// C5 D5 E5 F5 G5 A5 B5
	const double NOTE_FREQ[NOTE_COUNT] = {523.25, 587.33, 659.25, 698.46, 783.99, 880.00, 987.77};

	// Jump positions (player X) saved for the current level.
	std::vector<float> g_saved;
	// Jumps recorded in the current practice run.
	std::vector<float> g_recorded;
	size_t g_nextCue = 0;

	FMOD::Sound* g_sounds[NOTE_COUNT] = {nullptr};
	std::vector<short> g_pcm[NOTE_COUNT];

	std::string levelKey(GJGameLevel* level) {
		return fmt::format("jumps-{}", level->m_levelID.value());
	}

	// Build a piano-like note: a few decaying harmonics with a short attack.
	void buildNote(int idx) {
		if (!g_pcm[idx].empty()) return;
		const int rate = 44100;
		const int n = static_cast<int>(rate * 0.8);
		const double f = NOTE_FREQ[idx];
		const double amps[] = {1.0, 0.5, 0.35, 0.2, 0.12, 0.08};
		std::vector<double> buf(n, 0.0);
		double peak = 0.0;
		for (int i = 0; i < n; i++) {
			double t = static_cast<double>(i) / rate;
			double s = 0.0;
			for (int h = 0; h < 6; h++) {
				s += amps[h] * std::sin(2.0 * M_PI * f * (h + 1) * t) * std::exp(-t * (3.0 + h * 1.5));
			}
			s *= std::min(1.0, t / 0.005);
			buf[i] = s;
			peak = std::max(peak, std::fabs(s));
		}
		g_pcm[idx].resize(n);
		for (int i = 0; i < n; i++) {
			g_pcm[idx][i] = static_cast<short>(buf[i] / peak * 0.6 * 32767.0);
		}
	}

	void playNote(int idx) {
		auto engine = FMODAudioEngine::sharedEngine();
		if (!engine || !engine->m_system) return;
		auto sys = engine->m_system;
		if (!g_sounds[idx]) {
			buildNote(idx);
			FMOD_CREATESOUNDEXINFO ex{};
			ex.cbsize = sizeof(ex);
			ex.length = static_cast<unsigned int>(g_pcm[idx].size() * sizeof(short));
			ex.numchannels = 1;
			ex.defaultfrequency = 44100;
			ex.format = FMOD_SOUND_FORMAT_PCM16;
			sys->createSound(
				reinterpret_cast<const char*>(g_pcm[idx].data()),
				FMOD_OPENRAW | FMOD_OPENMEMORY | FMOD_CREATESAMPLE,
				&ex, &g_sounds[idx]
			);
		}
		if (g_sounds[idx]) {
			FMOD::Channel* ch = nullptr;
			sys->playSound(g_sounds[idx], nullptr, false, &ch);
		}
	}

	// Approximate units per second for each speed portal.
	float unitsPerSecond(float speed) {
		if (speed < 0.8f) return 251.16f;
		if (speed < 1.0f) return 311.58f;
		if (speed < 1.2f) return 387.42f;
		if (speed < 1.45f) return 468.0f;
		return 576.0f;
	}

	size_t firstCueAfter(float x) {
		size_t i = 0;
		while (i < g_saved.size() && g_saved[i] <= x) i++;
		return i;
	}
}

class $modify(JumpPianoPlayLayer, PlayLayer) {
	struct Fields {
		std::vector<CCLayerColor*> keys;
	};

	void buildPiano() {
		auto win = CCDirector::get()->getWinSize();
		const float keyW = 30.f;
		const float keyH = 55.f;
		const float gap = 2.f;
		float total = NOTE_COUNT * keyW + (NOTE_COUNT - 1) * gap;
		float startX = (win.width - total) / 2.f;

		for (int i = 0; i < NOTE_COUNT; i++) {
			auto key = CCLayerColor::create(ccc4(255, 255, 255, 190), keyW, keyH);
			key->setPosition({startX + i * (keyW + gap), 4.f});
			m_uiLayer->addChild(key, 100);
			m_fields->keys.push_back(key);
		}
	}

	void flashKey(int idx) {
		if (idx < 0 || idx >= static_cast<int>(m_fields->keys.size())) return;
		auto key = m_fields->keys[idx];
		key->stopAllActions();
		key->setColor(ccc3(255, 200, 0));
		key->runAction(CCTintTo::create(0.3f, 255, 255, 255));
	}

	bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
		if (!PlayLayer::init(level, useReplay, dontCreateObjects)) return false;
		g_saved = Mod::get()->getSavedValue<std::vector<float>>(levelKey(level), {});
		std::sort(g_saved.begin(), g_saved.end());
		g_recorded.clear();
		g_nextCue = 0;
		buildPiano();
		return true;
	}

	void resetLevel() {
		PlayLayer::resetLevel();
		float x = m_player1->getPositionX();
		// Drop jumps recorded after the respawn point.
		g_recorded.erase(
			std::remove_if(g_recorded.begin(), g_recorded.end(), [x](float j) { return j >= x; }),
			g_recorded.end()
		);
		g_nextCue = firstCueAfter(x);
	}

	void postUpdate(float dt) {
		PlayLayer::postUpdate(dt);
		if (m_isPracticeMode || g_nextCue >= g_saved.size()) return;
		float lead = static_cast<float>(Mod::get()->getSettingValue<int64_t>("lead-ms")) / 1000.f;
		float dist = unitsPerSecond(m_player1->m_playerSpeed) * lead;
		float x = m_player1->getPositionX();
		while (g_nextCue < g_saved.size() && x >= g_saved[g_nextCue] - dist) {
			int idx = static_cast<int>(g_nextCue % NOTE_COUNT);
			playNote(idx);
			flashKey(idx);
			g_nextCue++;
		}
	}

	void levelComplete() {
		if (m_isPracticeMode && !g_recorded.empty()) {
			Mod::get()->setSavedValue(levelKey(m_level), g_recorded);
		}
		PlayLayer::levelComplete();
	}
};

class $modify(JumpPianoPlayer, PlayerObject) {
	bool pushButton(PlayerButton button) {
		auto pl = PlayLayer::get();
		if (pl && pl->m_isPracticeMode && this == pl->m_player1 && button == PlayerButton::Jump) {
			int idx = static_cast<int>(g_recorded.size() % NOTE_COUNT);
			g_recorded.push_back(this->getPositionX());
			static_cast<JumpPianoPlayLayer*>(pl)->flashKey(idx);
		}
		return PlayerObject::pushButton(button);
	}
};
