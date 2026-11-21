#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/PlayerObject.hpp>
#include <algorithm>
#include <cmath>
#include <vector>

using namespace geode::prelude;

namespace {
	// Jump positions (player X) saved for the current level.
	std::vector<float> g_saved;
	// Jumps recorded in the current practice run.
	std::vector<float> g_recorded;
	size_t g_nextCue = 0;

	FMOD::Sound* g_sound = nullptr;
	std::vector<short> g_pcm;

	std::string levelKey(GJGameLevel* level) {
		return fmt::format("jumps-{}", level->m_levelID.value());
	}

	// Build a piano-like C5 note: a few decaying harmonics with a short attack.
	void buildNote() {
		if (!g_pcm.empty()) return;
		const int rate = 44100;
		const int n = static_cast<int>(rate * 0.8);
		const double f = 523.25;
		const double amps[] = {1.0, 0.5, 0.35, 0.2, 0.12, 0.08};
		std::vector<double> buf(n, 0.0);
		double peak = 0.0;
		for (int i = 0; i < n; i++) {
			double t = static_cast<double>(i) / rate;
			double s = 0.0;
			for (int h = 0; h < 6; h++) {
				s += amps[h] * std::sin(2.0 * M_PI * f * (h + 1) * t) * std::exp(-t * (3.0 + h * 1.5));
			}
			double attack = std::min(1.0, t / 0.005);
			s *= attack;
			buf[i] = s;
			peak = std::max(peak, std::fabs(s));
		}
		g_pcm.resize(n);
		for (int i = 0; i < n; i++) {
			g_pcm[i] = static_cast<short>(buf[i] / peak * 0.6 * 32767.0);
		}
	}

	void playNote() {
		auto engine = FMODAudioEngine::sharedEngine();
		if (!engine || !engine->m_system) return;
		auto sys = engine->m_system;
		if (!g_sound) {
			buildNote();
			FMOD_CREATESOUNDEXINFO ex{};
			ex.cbsize = sizeof(ex);
			ex.length = static_cast<unsigned int>(g_pcm.size() * sizeof(short));
			ex.numchannels = 1;
			ex.defaultfrequency = 44100;
			ex.format = FMOD_SOUND_FORMAT_PCM16;
			sys->createSound(
				reinterpret_cast<const char*>(g_pcm.data()),
				FMOD_OPENRAW | FMOD_OPENMEMORY | FMOD_CREATESAMPLE,
				&ex, &g_sound
			);
		}
		if (g_sound) {
			FMOD::Channel* ch = nullptr;
			sys->playSound(g_sound, nullptr, false, &ch);
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
	bool init(GJGameLevel* level, bool useReplay) {
		if (!PlayLayer::init(level, useReplay)) return false;
		g_saved = Mod::get()->getSavedValue<std::vector<float>>(levelKey(level), {});
		std::sort(g_saved.begin(), g_saved.end());
		g_recorded.clear();
		g_nextCue = 0;
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
			playNote();
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
			g_recorded.push_back(this->getPositionX());
		}
		return PlayerObject::pushButton(button);
	}
};
