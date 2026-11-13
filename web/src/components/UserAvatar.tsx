import { AVATAR_ART } from "./avatarArt.generated";

type AvatarProps = {
  name: string;
  color: string;
  avatarId?: number;
  size?: number;
};

export function UserAvatar({ name, color, avatarId = 0, size = 32 }: AvatarProps) {
  const valid = Number.isInteger(avatarId) && avatarId >= 1 && avatarId <= 30;
  const words = name.trim().split(/\s+/).filter(Boolean);
  const initials = (words.length > 1 ? [words[0], words[words.length - 1]] : words)
    .map((part) => Array.from(part)[0]?.toUpperCase() ?? "")
    .join("") || "?";

  return (
    <span
      className="relative inline-flex shrink-0 items-center justify-center overflow-hidden rounded-full font-semibold"
      style={{
        width: size,
        height: size,
        backgroundColor: valid ? "#1C1716" : color,
        color: valid ? "#FFFFFF" : "#1A1412",
        fontSize: Math.max(12, Math.round(size * 0.38)),
      }}
      aria-hidden="true"
    >
      {valid ? (
        <>
          <img
            src={AVATAR_ART[avatarId - 1]}
            alt=""
            draggable={false}
            className="absolute inset-0 h-full w-full rounded-full object-cover"
          />
          {size >= 40 ? (
            <span
              className="absolute bottom-0 right-0 rounded-md bg-espresso-950/95 px-0.5 text-cream-50"
              style={{ fontSize: Math.max(12, Math.round(size * 0.22)), lineHeight: 1.3 }}
            >
              {initials}
            </span>
          ) : null}
        </>
      ) : (
        <span>{initials}</span>
      )}
    </span>
  );
}
