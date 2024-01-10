"""Remove macrocontext field

Revision ID: 1c55c341360f
Revises: 39428242f7f5
Create Date: 2024-01-09 15:01:39.698918

"""

# revision identifiers, used by Alembic.
revision = '1c55c341360f'
down_revision = '39428242f7f5'

from alembic import op
import sqlalchemy as sa


def upgrade():
    op.drop_column('voicemail_messages', 'macrocontext')


def downgrade():
    op.add_column('voicemail_messages', sa.Column('macrocontext', sa.String(80)))
