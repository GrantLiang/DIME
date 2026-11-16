/********************************************************************************
* UILayoutParityTest: the window layout calculations of dime-ui (layout /
* candwnd via dime_ui_*) against the golden files in src/tests/golden/UILayout,
* recorded while the C++ originals (UILayout::Cpp, removed in phase 6b-3) still
* matched them: RectInRect, the work-area probe point, the placement around the
* composition (below / above / docked, multiple monitors with negative
* coordinates), and the notify-window stack (grow direction, new-slot position,
* restacking with and without a candidate window); MulDiv against Win32's; the
* candidate window (metrics, size, rows, truncation, widening, hit tests, page
* indicator) and the scroll bar (buttons, track, thumb, hit test, idle line,
* arrows, dragging, clamping, placement).
*
* The random draws are the ones the recordings were made with: do not reorder
* them (DIME_GOLDEN_RECORD=1 re-records after a deliberate change).
********************************************************************************/
#include "pch.h"
#include "CppUnitTest.h"
#include <Windows.h>
#include <climits>
#include <cstring>
#include <random>
#include <string>
#include <vector>

#include "dime_ffi.h"
#include "Golden.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace
{
    std::wstring R(const RECT& r)
    {
        return L"(" + std::to_wstring(r.left) + L"," + std::to_wstring(r.top) + L"," + std::to_wstring(r.right) + L"," + std::to_wstring(r.bottom) + L")";
    }

    RECT RandomRect(std::mt19937& rng, int lo, int hi, int maxSize)
    {
        // Golden::Pick: the inputs are behind golden files (phase 6b).
        RECT r;
        r.left = Golden::Pick(rng, lo, hi);
        r.top = Golden::Pick(rng, lo, hi);
        r.right = r.left + Golden::Pick(rng, 0, maxSize);
        r.bottom = r.top + Golden::Pick(rng, 0, maxSize);
        return r;
    }

    // The C++ original asserted (Debug) for a target wider / taller than the
    // limit that sticks out on both sides (real windows are never that
    // large), so the recordings skip those inputs.
    bool Straddles(const RECT& limit, const RECT& target)
    {
        return (target.left < limit.left && target.right > limit.right) ||
               (target.top < limit.top && target.bottom > limit.bottom);
    }
}

namespace DIMEUnitTests
{
    TEST_CLASS(UILayoutParityTest)
    {
    public:
        TEST_METHOD(RectInRect)
        {
            std::mt19937 rng(1);
            int compared = 0;
            Golden::Log golden;
            Golden::Chunks ch(golden, L"rects", 20000);
            for (int i = 0; i < 200000; i++)
            {
                RECT limit = RandomRect(rng, -3000, 3000, 2500);
                RECT target = RandomRect(rng, -4000, 4000, 2000);
                if (Straddles(limit, target)) continue;
                uint32_t r = dime_ui_rect_in_rect((const DimeRect*)&limit, (const DimeRect*)&target);
                ch.Add(R(limit) + R(target) + L"=" + std::to_wstring(r));
                ch.Next();
                compared++;
            }
            ch.Finish();
            Golden::Check(L"UILayout", L"RectInRect", golden);
            Logger::WriteMessage((std::to_wstring(compared) + L" rectangles").c_str());
        }

        TEST_METHOD(WindowPlacement)
        {
            std::mt19937 rng(2);
            int compared = 0;
            Golden::Log golden;
            Golden::Chunks ch(golden, L"placements", 20000);
            for (int i = 0; i < 200000; i++)
            {
                // A work area (possibly a monitor left of / above the primary one).
                RECT work = RandomRect(rng, -3000, 2000, 0);
                work.right = work.left + 800 + (int)(rng() % 3000);
                work.bottom = work.top + 600 + (int)(rng() % 2000);
                // The window, smaller than the work area.
                RECT window = { 0, 0, 50 + (int)(rng() % 700), 30 + (int)(rng() % 550) };
                // The text extent: a caret / composition near the work area, or a large area.
                RECT text;
                if (Golden::Pick(rng, 0, 9) < 8)
                {
                    text = RandomRect(rng, 0, 0, 0);
                    text.left = work.left - 200 + (int)(rng() % (work.right - work.left + 400));
                    text.top = work.top - 200 + (int)(rng() % (work.bottom - work.top + 400));
                    text.right = text.left + (int)(rng() % 300);
                    text.bottom = text.top + 10 + (int)(rng() % 40);
                }
                else
                {
                    text = RandomRect(rng, -3000, 3000, 2500);
                }

                DimePoint pr = { 0, 0 };
                dime_ui_work_area_probe((const DimeRect*)&text, (const DimeRect*)&window, &pr);

                // Skip the inputs the C++ original asserted on in RectInRect.
                RECT below = window, above = window;
                LONG baseX = (text.right - text.left) > (window.right - window.left) ? text.left : text.right;
                OffsetRect(&below, baseX, text.bottom + 7);
                OffsetRect(&above, baseX, text.top - (window.bottom - window.top) - 7);
                ch.Add(R(text) + R(window) + L" probe " + std::to_wstring(pr.x) + L"," + std::to_wstring(pr.y));
                if (Straddles(work, below) || Straddles(work, above)) { ch.Next(); continue; }

                DimePoint r = { -12345, -12345 };
                dime_ui_fit_point((const DimeRect*)&text, (const DimeRect*)&work, (const DimeRect*)&window, &r);
                ch.Add(R(work) + L" fit " + std::to_wstring(r.x) + L"," + std::to_wstring(r.y));
                ch.Next();
                compared++;
            }
            ch.Finish();
            Golden::Check(L"UILayout", L"WindowPlacement", golden);
            Logger::WriteMessage((std::to_wstring(compared) + L" placements").c_str());
        }

        TEST_METHOD(NotifyStack)
        {
            std::mt19937 rng(3);
            Golden::Log golden;
            Golden::Chunks ch(golden, L"stacks", 10000);
            for (int i = 0; i < 100000; i++)
            {
                size_t count = 1 + rng() % 6;
                std::vector<DimeNotifySlot> slots(count);
                for (auto& s : slots)
                {
                    bool exists = rng() % 5 != 0;
                    s.visible = exists && rng() % 3 != 0;
                    s.width = exists ? 40 + (int)(rng() % 300) : 0;
                    s.height = exists ? 20 + (int)(rng() % 80) : 0;
                }
                BOOL candVisible = rng() % 2;
                RECT cand = RandomRect(rng, -2000, 2000, 600);
                POINT candLoc = { (LONG)(rng() % 4000) - 2000, (LONG)(rng() % 4000) - 2000 };
                POINT notify = { (LONG)(rng() % 4000) - 2000, (LONG)(rng() % 4000) - 2000 };
                int screenBottom = 600 + (int)(rng() % 2000);
                BOOL growUp = rng() % 2;
                size_t newSlot = rng() % (count + 1);   // includes one past the end

                int gr = dime_ui_stack_grow_up(candVisible, cand.top, notify.y, slots.data(), count, screenBottom);
                ch.Add(slots.data(), count * sizeof(slots[0]));
                ch.Add(L"in " + std::to_wstring(candVisible) + R(cand) + std::to_wstring(candLoc.x) + L"," + std::to_wstring(candLoc.y) + L" " +
                    std::to_wstring(notify.x) + L"," + std::to_wstring(notify.y) + L" " + std::to_wstring(screenBottom) + L" " +
                    std::to_wstring(growUp) + L" " + std::to_wstring(newSlot) + L" grow " + std::to_wstring(gr));

                DimePoint pr = { 0, 0 };
                dime_ui_stack_pos_for_new_slot({ notify.x, notify.y }, slots.data(), count, newSlot, growUp, &pr);
                ch.Add(L"new " + std::to_wstring(pr.x) + L"," + std::to_wstring(pr.y));

                std::vector<POINT> posR(count);
                std::vector<BOOL> placedR(count);
                dime_ui_restack(candVisible, { cand.left, cand.top, cand.right, cand.bottom }, { candLoc.x, candLoc.y }, { notify.x, notify.y },
                    slots.data(), count, growUp, (DimePoint*)posR.data(), (int32_t*)placedR.data());
                for (size_t k = 0; k < count; k++)
                    ch.Add(L"slot " + std::to_wstring(placedR[k]) + (placedR[k] ? L" " + std::to_wstring(posR[k].x) + L"," + std::to_wstring(posR[k].y) : L""));
                ch.Next();
            }
            ch.Finish();
            Golden::Check(L"UILayout", L"NotifyStack", golden);
        }

        // ---- Candidate window and scroll bar (phase 3b) ----

        TEST_METHOD(MulDivMatchesWin32)
        {
            std::mt19937 rng(10);
            const int edges[] = { 0, 1, -1, 2, -2, 3, 96, 100, 0x7fffffff, (int)0x80000000, 0x7ffffffe, -0x7fffffff, 46341, -46341 };
            int compared = 0;
            auto check = [&](int a, int b, int c)
            {
                int w = ::MulDiv(a, b, c);
                int r = dime_ui_mul_div(a, b, c);
                if (w != r)
                    Assert::Fail((L"MulDiv(" + std::to_wstring(a) + L"," + std::to_wstring(b) + L"," + std::to_wstring(c) +
                        L"): Win32 " + std::to_wstring(w) + L" Rust " + std::to_wstring(r)).c_str());
                compared++;
            };
            for (int a : edges) for (int b : edges) for (int c : edges) check(a, b, c);
            std::uniform_int_distribution<int> nearDist(-3000, 3000), anyDist(INT_MIN, INT_MAX);
            for (int i = 0; i < 300000; i++)
            {
                check(nearDist(rng), nearDist(rng), nearDist(rng));
                check(anyDist(rng), nearDist(rng), nearDist(rng));
                check(anyDist(rng), anyDist(rng), anyDist(rng));
            }
            Logger::WriteMessage((std::to_wstring(compared) + L" MulDiv calls").c_str());
        }

        TEST_METHOD(CandidateWindowLayout)
        {
            std::mt19937 rng(11);
            const UINT dpis[] = { 96, 120, 144, 168, 192, 240, 288, 384 };
            int compared = 0;
            Golden::Log golden;
            Golden::Chunks ch(golden, L"layouts", 5000);
            auto n = [](int v) { return std::to_wstring(v) + L","; };
            for (int i = 0; i < 100000; i++)
            {
                UINT wndWidth = 1 + rng() % 20;
                if (rng() % 50 == 0) wndWidth = 1 + rng() % 5000;
                SIZE measured = { (LONG)(rng() % 3000), (LONG)(rng() % 200) };
                int ave = (int)(rng() % 60);
                UINT dpi = (rng() % 4 == 0) ? 48 + rng() % 500 : dpis[rng() % 8];
                int sb = ::MulDiv(16, dpi, 96);

                // WM_CREATE
                int t2 = -9, r2 = -9, p2 = -9;
                dime_ui_cand_initial_metrics(measured.cx, measured.cy, wndWidth, &t2, &r2, &p2);
                ch.Add(L"in " + n(wndWidth) + n(measured.cx) + n(measured.cy) + n(ave) + n(dpi) + L" init " + n(t2) + n(r2) + n(p2));

                // _DrawList metrics
                DimeCandMetrics mr = {};
                dime_ui_cand_metrics(measured.cx, measured.cy, ave, wndWidth, &mr);
                ch.Add(&mr, sizeof(mr));

                int rows = 1 + (int)(rng() % 10);
                int32_t sw = 0, sh = 0;
                dime_ui_cand_window_size(mr.cx_title, mr.cy_line, mr.padding, rows, sb, &sw, &sh);
                ch.Add(L"rows " + n(rows) + L"size " + n(sw) + n(sh));

                RECT client = { 0, 0, sw - 2, sh - 2 };
                if (rng() % 4 == 0) client = RandomRect(rng, -50, 50, 600);
                RECT br = {};
                dime_ui_cand_scrollbar_rect((const DimeRect*)&client, sb, dpi, 10, (DimeRect*)&br);
                ch.Add(L"client " + R(client) + L" sb " + R(br));

                for (int row = 0; row < rows; row++)
                {
                    DimeCandRow rr = {};
                    dime_ui_cand_row((const DimeRect*)&client, row, &mr, sb, dpi, &rr);
                    ch.Add(&rr, sizeof(rr));
                }

                int length = (int)(rng() % 40);
                int tr = 0;
                int lr = dime_ui_cand_truncate(length, &tr);

                int measuredCx = (int)(rng() % 4000);
                int titleR = mr.cx_title;
                UINT widthR = wndWidth;
                int wr = dime_ui_cand_widen(lr, measuredCx, mr.left_padding + mr.cx_line, &titleR, &widthR);
                ch.Add(L"item " + n(length) + n(lr) + n(tr) + n(dime_ui_cand_item_needs_measure(lr, wndWidth)) + n(measuredCx) +
                    L"widen " + n(wr) + n(titleR) + n((int)widthR) + L"resize " + n(dime_ui_cand_needs_resize(titleR, (const DimeRect*)&client, sb)));

                UINT pageStart = rng() % 30, count = rng() % 40;
                for (int k = 0; k < 8; k++)
                {
                    POINT pt = { client.left - 5 + (LONG)(rng() % (client.right - client.left + 10 + 1)),
                                 client.top - 5 + (LONG)(rng() % (client.bottom - client.top + 10 + 1)) };
                    if (client.right < client.left || client.bottom < client.top) pt = { (LONG)(rng() % 100), (LONG)(rng() % 100) };
                    int hr = dime_ui_cand_hit_row((const DimeRect*)&client, mr.padding, sb, mr.cy_line, (UINT)rows, pageStart, count, { pt.x, pt.y });
                    ch.Add(L"hit " + n(pageStart) + n(count) + n(pt.x) + n(pt.y) + n(hr));
                }

                RECT ar = {};
                dime_ui_cand_mouse_area((const DimeRect*)&client, sb, 0, (DimeRect*)&ar);
                ch.Add(L"column " + R(ar));
                dime_ui_cand_mouse_area((const DimeRect*)&client, sb, 1, (DimeRect*)&ar);
                ch.Add(L"list " + R(ar));
                dime_ui_cand_page_indicator((const DimeRect*)&client, mr.cy_line, rows, sb, dpi, (DimeRect*)&ar);
                ch.Add(L"page " + R(ar));

                (void)rng();   // the page indicator font height's draw (a MulDiv, covered by MulDivMatchesWin32)
                ch.Next();
                compared++;
            }
            ch.Finish();
            Golden::Check(L"UILayout", L"CandidateWindowLayout", golden);
            Logger::WriteMessage((std::to_wstring(compared) + L" candidate layouts").c_str());
        }

        TEST_METHOD(ScrollBarLayout)
        {
            std::mt19937 rng(12);
            const UINT dpis[] = { 96, 120, 144, 168, 192, 240, 288, 384 };
            int compared = 0;
            Golden::Log golden;
            Golden::Chunks ch(golden, L"scroll bars", 10000);
            auto n = [](int v) { return std::to_wstring(v) + L","; };
            for (int i = 0; i < 200000; i++)
            {
                UINT dpi = (rng() % 4 == 0) ? 48 + rng() % 500 : dpis[rng() % 8];
                RECT client = { 0, 0, (LONG)(rng() % 50), (LONG)(rng() % 900) - 20 };
                if (rng() % 5 == 0) client = RandomRect(rng, -40, 40, 400);
                DimeScrollInfo si = { (int)(rng() % 300) - 5, (int)(rng() % 40) - 3, (int)(rng() % 320) - 10 };

                DimeScrollParts pr = {};
                dime_ui_sb_parts((const DimeRect*)&client, dpi, &si, &pr);
                ch.Add(L"in " + n(dpi) + R(client) + n(si.max) + n(si.page) + n(si.pos));
                ch.Add(&pr, sizeof(pr));

                for (int k = 0; k < 6; k++)
                {
                    POINT pt = { (LONG)(rng() % 60) - 5 + client.left, (LONG)(rng() % 920) - 10 + client.top };
                    ch.Add(L"hit " + n(pt.x) + n(pt.y) + n(dime_ui_sb_hit_test((const DimeRect*)&client, dpi, &si, { pt.x, pt.y })));
                }

                // The paint path only draws the idle thumb for nMax > nPage.
                if (si.max > si.page)
                {
                    RECT ir = {};
                    int hr = dime_ui_sb_idle_thumb((const DimeRect*)&client, dpi, &si, (DimeRect*)&ir);
                    ch.Add(L"idle " + n(hr) + (hr ? R(ir) : L""));
                }

                DimeScrollArrows wr = {};
                dime_ui_sb_arrows((const DimeRect*)&client, dpi, &wr);
                ch.Add(&wr, sizeof(wr));

                int start = (int)(rng() % 300), delta = (int)(rng() % 2000) - 1000;
                int dr = -7;
                int gr = dime_ui_sb_drag_pos((const DimeRect*)&client, dpi, &si, start, delta, &dr);
                ch.Add(L"drag " + n(start) + n(delta) + n(gr) + (gr ? n(dr) : L"") + L"clamp " + n(dime_ui_sb_clamp_pos(si.pos, si.max)));

                int pw = (int)(rng() % 600), ph = (int)(rng() % 900);
                RECT tr = {};
                dime_ui_sb_attached_rect(pw, ph, ::MulDiv(16, dpi, 96), (DimeRect*)&tr);
                ch.Add(L"attached " + n(pw) + n(ph) + R(tr));
                ch.Next();
                compared++;
            }
            ch.Finish();
            Golden::Check(L"UILayout", L"ScrollBarLayout", golden);
            Logger::WriteMessage((std::to_wstring(compared) + L" scroll bar layouts").c_str());
        }
    };
}
